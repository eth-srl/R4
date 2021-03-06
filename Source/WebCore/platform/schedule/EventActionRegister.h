/*
 * EventActionRegister.h
 *
 *  Created on: Oct 1, 2013
 *      Author: veselin
 */

#ifndef EVENTACTIONREGISTER_H_
#define EVENTACTIONREGISTER_H_

#include <vector>
#include <ostream>
#include <map>

#include "wtf/EventActionDescriptor.h"
#include "wtf/EventActionSchedule.h"

#include "wtf/ActionLogReport.h"

namespace WebCore {

typedef bool (*EventActionHandlerFunction)(void* object, const WTF::EventActionDescriptor& descriptor);

class EventActionRegisterMaps;

/**
 * The EventActionRegister maintains a register of event actions pending execution.
 *
 * A scheduler is used to decide when an event action is executed.
 *
 * Two types of handlers can be registered for event actions, event action handlers and event action providers.
 *
 * Event action handlers:
 *    A one-shot handler registered for one specific event action. Matches on event action type and params.
 *
 * Event action provider:
 *    A handler that gets the chance to handle any event action of a particular type before matching with
 *    event action handlers.
 *
 */
class EventActionRegister {
    WTF_MAKE_NONCOPYABLE(EventActionRegister);

public:
    EventActionRegister();
    virtual ~EventActionRegister();

    // Registration of event action providers and handlers
    void registerEventActionProvider(const std::string& type, EventActionHandlerFunction f, void* object);
    void registerEventActionHandler(const WTF::EventActionDescriptor& descriptor, EventActionHandlerFunction f, void* object);
    void deregisterEventActionHandler(const WTF::EventActionDescriptor& descriptor);

    // Attempts to execute an event action. Returns true on success.
    bool runEventAction(const WTF::EventActionDescriptor& descriptor);
    bool runEventAction(WTF::EventActionId newEventActionId, WTF::EventActionId originalEventActionId, const WTF::EventActionDescriptor& descriptor);

    // Ghost operations, used to simulate the execution of certain un-schedulable event actions
    // TODO(WebERA) Ghost operations are disabled, and have no effect. Thus, event actions which use this feature are not detected
    // and controlled. Until we find a good way to isolate and control these event actions (two at the moment), these functions will
    // remain disabled.
    void enterGhostEventAction(WTF::EventActionId id, ActionLog::EventActionType type);
    void exitGhostEventAction();

    // Immediate operations, used to mark the current executing timer as a schedulable event action
    // These are only feasible if combined with a event action provider for replay
    void enterImmediateEventAction(ActionLog::EventActionType type, const WTF::EventActionDescriptor& descriptor);
    void exitImmediateEventAction();

    // Used to inspect the currently executed event action globally
    const WTF::EventActionDescriptor& currentEventActionDispatching() const
    {
        if (m_isDispatching) {
            return m_dispatchHistory->isEmpty() ? WTF::EventActionDescriptor::null : m_dispatchHistory->last().second;
        }

        return WTF::EventActionDescriptor::null;
    }

    EventActionSchedule* dispatchHistory() { return m_dispatchHistory; }

    std::set<std::string> getWaitingNames();

    void debugPrintNames(std::ostream& out) const;

    ActionLog::EventActionType toActionLogType(WTF::EventActionCategory category) {

        // TODO(WebERA-HB-REVIEW): I have retained the old ActionLog types, but I don't know if we can just add in our slightly different types (or if these types are correct).

        switch (category) {
        case WTF::OTHER:
        case WTF::PARSING:
            return ActionLog::UNKNOWN;
            break;
        case WTF::TIMER:
            return ActionLog::TIMER;
            break;
        case WTF::USER_INTERFACE:
            return ActionLog::USER_INTERFACE;
            break;
        case WTF::NETWORK:
            return ActionLog::NETWORK;
            break;

        default:
            ASSERT_NOT_REACHED();
            return ActionLog::UNKNOWN;
        }
    }

    void setVerbose(bool v) {
        m_verbose = v;
    }

    WTF::EventActionId translateOldIdToNew(WTF::EventActionId oldId) {
        std::map<int, int>::const_iterator it = m_originalToNewEventActionIdMap.find(oldId);

        if (it == m_originalToNewEventActionIdMap.end()) {
            return oldId;
        }

        return it->second;
    }

private:

    void eventActionDispatchStart(WTF::EventActionId id, WTF::EventActionId originalId, const WTF::EventActionDescriptor& descriptor)
    {
        ASSERT(!m_isDispatching);

        m_originalToNewEventActionIdMap.insert(std::pair<int, int>(originalId, id));

        m_dispatchHistory->append(EventActionScheduleItem(id, descriptor));
        m_isDispatching = true;
    }

    void eventActionDispatchEnd(bool commit, WTF::EventActionId originalId)
    {
        ASSERT(m_isDispatching);

        m_isDispatching = false;

        if (!commit) {
            m_originalToNewEventActionIdMap.erase(originalId);
            m_dispatchHistory->removeLast();
        }
    }

    EventActionRegisterMaps* m_maps;
    bool m_isDispatching;

    EventActionSchedule* m_dispatchHistory;

    bool m_verbose;

    std::map<int, int> m_originalToNewEventActionIdMap;
};

}  // namespace WebCore

#endif /* EVENTACTIONREGISTER_H_ */
