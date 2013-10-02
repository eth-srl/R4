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
#include <wtf/CurrentTime.h>
#include <wtf/MainThread.h>
#include <wtf/Vector.h>
#include <stdio.h>
#include <iostream>

#include <platform/schedule/DefaultScheduler.h>
#include <platform/schedule/RepeatScheduler.h>
#include <platform/schedule/TaskRegister.h>

using namespace std;

namespace WebCore {

// Fire timers for this length of time, and then quit to let the run loop process user input events.
// 100ms is about a perceptable delay in UI, so use a half of that as a threshold.
// This is to prevent UI freeze when there are too many timers or machine performance is low.
static const double maxDurationOfFiringTimers = 0.050;

Scheduler* ThreadTimers::m_scheduler = new DefaultScheduler();
EventActionSchedule ThreadTimers::m_eventActionSchedule;
EventActionsHB ThreadTimers::m_eventActionsHB;

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
	, m_taskRegister(new TaskRegister)
{
    if (isMainThread())
        setSharedTimer(mainThreadSharedTimer());
}

ThreadTimers::~ThreadTimers()
{
	delete m_taskRegister;
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
        
    if (m_firingTimers || m_timerHeap.isEmpty())
        m_sharedTimer->stop();
    else
        m_sharedTimer->setFireInterval(max(m_timerHeap.first()->m_nextFireTime - monotonicallyIncreasingTime(), 0.0));
}

void ThreadTimers::sharedTimerFired()
{
    // Redirect to non-static method.
    threadGlobalData().threadTimers().sharedTimerFiredInternal();
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

        int timer_index = ThreadTimers::m_scheduler->selectNextSchedulableItem(m_timerHeap);

        if (timer_index == Scheduler::YIELD) {
            break;
        }

        TimerBase* timer = m_timerHeap.at(timer_index);

        timer->m_nextFireTime = 0;
        timer->heapDelete();

        double interval = timer->repeatInterval();
        timer->setNextFireTime(interval ? fireTime + interval : 0);

        // WebERA: Denote that currently a timer with a given name is executed.
        ThreadTimers::eventActionSchedule().eventActionDispatchStart(timer->eventActionDescriptor());

        // Once the timer has been fired, it may be deleted, so do nothing else with it after this point.
        timer->fired();

        // WebERA: Denote that we are finished dispatching this event action
        ThreadTimers::eventActionSchedule().eventActionDispatchEnd();

        // Catch the case where the timer asked timers to fire in a nested event loop, or we are over time limit.
        if (!m_firingTimers || timeToQuit < monotonicallyIncreasingTime())
            break;

    }

    m_firingTimers = false;

    updateSharedTimer();
}

void ThreadTimers::fireTimersInNestedEventLoop()
{
    // Reset the reentrancy guard so the timers can fire again.
    m_firingTimers = false;
    updateSharedTimer();
}

} // namespace WebCore

