/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ThreadTimers.h"

#include "SharedTimer.h"
#include "ThreadGlobalData.h"
#include "Timer.h"
#include <wtf/ActionLogReport.h>
#include <wtf/CurrentTime.h>
#include <wtf/MainThread.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/Vector.h>
#include <stdio.h>
#include <iostream>

#include <platform/schedule/DefaultScheduler.h>
#include <platform/schedule/EventActionRegister.h>

#include <vector>
#include <set>

using namespace std;

namespace WebCore {

// Fire timers for this length of time, and then quit to let the run loop process user input events.
// 100ms is about a perceptable delay in UI, so use a half of that as a threshold.
// This is to prevent UI freeze when there are too many timers or machine performance is low.
static const double maxDurationOfFiringTimers = 0.050;

Scheduler* ThreadTimers::m_scheduler = new DefaultScheduler();

void ThreadTimers::setScheduler(Scheduler* scheduler)
{
    delete ThreadTimers::m_scheduler;
    ThreadTimers::m_scheduler = scheduler;
}

// Timers are created, started and fired on the same thread, and each thread has its own ThreadTimers
// copy to keep the heap and a set of currently firing timers.

static MainThreadSharedTimer* mainThreadSharedTimer()
{
    static MainThreadSharedTimer* timer = new MainThreadSharedTimer;
    return timer;
}

ThreadTimers::ThreadTimers()
    : m_sharedTimer(0)
    , m_firingTimers(false)
{
    if (isMainThread())
        setSharedTimer(mainThreadSharedTimer());
}

ThreadTimers::~ThreadTimers()
{
}

// A worker thread may initialize SharedTimer after some timers are created.
// Also, SharedTimer can be replaced with 0 before all timers are destroyed.
void ThreadTimers::setSharedTimer(SharedTimer* sharedTimer)
{
    if (m_sharedTimer) {
        m_sharedTimer->setFiredFunction(0);
        m_sharedTimer->stop();
    }
    
    m_sharedTimer = sharedTimer;
    
    if (sharedTimer) {
        m_sharedTimer->setFiredFunction(ThreadTimers::sharedTimerFired);
        updateSharedTimer();
    }
}

void ThreadTimers::updateSharedTimer()
{
    if (!m_sharedTimer)
        return;
        
    if (m_firingTimers || m_timerHeap.isEmpty()) {
    	// WebERA: Regardless of whether there are timers, keep running every 50ms milliseconds.
    	// This is to allow for delayed events to trigger.
        m_sharedTimer->setFireInterval(0.05);

    } else {
        // WebERA: Furthermore, force the timer to yield after 1 second regardless of other scheduled
        // timers. This ensures that we can timely timeout even if a slow running timer exist.

        // Notice! That we could timeout on an event action that has just not been scheduled in the
        // scheduler yet, but is in the queue of timers. We assume that the sites we are testing are
        // executed with a certain speed, and slow appearing event actions indicate an error of some sort.
        m_sharedTimer->setFireInterval(min(max(m_timerHeap.first()->m_nextFireTime - monotonicallyIncreasingTime(), 0.0), 1.0));
    }
}

void ThreadTimers::sharedTimerFired()
{
    // Redirect to non-static method.
    threadGlobalData().threadTimers().sharedTimerFiredInternal();
}

bool ThreadTimers::fireTimerCallback(void* object, const WTF::EventActionDescriptor&) {
	TimerBase* timer = (TimerBase*)object;
    timer->inEventActionRegister(false);

    // Implicit happens before

    if (HBIsCurrentEventActionValid()) {
        timer->m_lastFireEventAction = HBCurrentEventAction();
    } else {
        timer->m_lastFireEventAction = 0;
    }

    if (timer->m_starterEventAction != 0 && HBIsCurrentEventActionValid() && !timer->m_disableImplicitHappensBeforeRelations) {
        if (timer->m_ignoreFireIntervalForHappensBefore) {
            HBAddExplicitArc(timer->m_starterEventAction, HBCurrentEventAction());
        } else {
            HBAddTimedArc(timer->m_starterEventAction, HBCurrentEventAction(), timer->m_nextFireInterval);
        }
    }

    // Set next fire time

    double interval = timer->repeatInterval();
    timer->setNextFireTime(interval ? monotonicallyIncreasingTime() + interval : 0, interval);

    // Once the timer has been fired, it may be deleted, so do nothing else with it after this point.
    timer->fired();

    return true;
}

void ThreadTimers::sharedTimerFiredInternal()
{
    // Do a re-entrancy check.
    if (m_firingTimers)
        return;
    m_firingTimers = true;

    double fireTime = monotonicallyIncreasingTime();
    double timeToQuit = fireTime + maxDurationOfFiringTimers;

    while (!m_timerHeap.isEmpty() && m_timerHeap.first()->m_nextFireTime <= fireTime) {
        TimerBase* timer = m_timerHeap.at(0);

        timer->m_nextFireTime = 0;
        timer->heapDelete();

        if (timer->eventActionDescriptor().isNull()) {
        	// Run the timer immediately.
            fireTimerCallback(timer, timer->eventActionDescriptor());
        } else {
        	// Run the timer through the scheduler.
            timer->inEventActionRegister(true);
            eventActionRegister()->registerEventActionHandler(
                        timer->eventActionDescriptor(),
                        &fireTimerCallback,
                        timer);
            m_scheduler->eventActionScheduled(timer->eventActionDescriptor(), eventActionRegister());
        }

        // Catch the case where the timer asked timers to fire in a nested event loop, or we are over time limit.
        if (!m_firingTimers || timeToQuit < monotonicallyIncreasingTime())
            break;

    }

    m_scheduler->executeDelayedEventActions(eventActionRegister());

    m_firingTimers = false;

    updateSharedTimer();
}

void ThreadTimers::deregisterEventActionHandler(TimerBase* timer)
{
    eventActionRegister()->deregisterEventActionHandler(timer->eventActionDescriptor());
    m_scheduler->eventActionDescheduled(timer->eventActionDescriptor(), eventActionRegister());
}

void ThreadTimers::fireTimersInNestedEventLoop()
{
    // Reset the reentrancy guard so the timers can fire again.
    m_firingTimers = false;
    updateSharedTimer();
}

} // namespace WebCore

