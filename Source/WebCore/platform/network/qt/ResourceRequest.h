/*
 * Copyright (C) 2003, 2006 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) 2006 Samuel Weinig <sam.weinig@gmail.com>
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

#ifndef ResourceRequest_h
#define ResourceRequest_h

#include <iostream>

#include "ResourceRequestBase.h"
#include "wtf/EventActionDescriptor.h"
#include "WebCore/platform/EventActionHappensBeforeReport.h"

QT_BEGIN_NAMESPACE
class QNetworkRequest;
QT_END_NAMESPACE

namespace WebCore {
class NetworkingContext;

    class ResourceRequest : public ResourceRequestBase {
    public:
        ResourceRequest(const String& url) 
            : ResourceRequestBase(KURL(ParsedURLString, url), UseProtocolCachePolicy)
            , m_calleeEventAction(HBIsCurrentEventActionValid() ? HBCurrentEventAction() : 0)
            , m_senderEventAction(0)
        {
        }

        ResourceRequest(const KURL& url) 
            : ResourceRequestBase(url, UseProtocolCachePolicy)
            , m_calleeEventAction(HBIsCurrentEventActionValid() ? HBCurrentEventAction() : 0)
            , m_senderEventAction(0)
        {
        }

        ResourceRequest(const KURL& url, const String& referrer, ResourceRequestCachePolicy policy = UseProtocolCachePolicy) 
            : ResourceRequestBase(url, policy)
            , m_calleeEventAction(HBIsCurrentEventActionValid() ? HBCurrentEventAction() : 0)
            , m_senderEventAction(0)
        {
            setHTTPReferrer(referrer);
        }

        ResourceRequest()
            : ResourceRequestBase(KURL(), UseProtocolCachePolicy)
            , m_calleeEventAction(HBIsCurrentEventActionValid() ? HBCurrentEventAction() : 0)
            , m_senderEventAction(0)
        {
        }

        QNetworkRequest toNetworkRequest(NetworkingContext* = 0) const;

        void setCalleeEventAction(WTF::EventActionId callee) {
            m_calleeEventAction = callee;
        }

        void setSenderEventAction(WTF::EventActionId sender) {
             m_senderEventAction = sender;
        }

    private:
        friend class ResourceRequestBase;

        void doUpdatePlatformRequest() {}
        void doUpdateResourceRequest() {}

        PassOwnPtr<CrossThreadResourceRequestData> doPlatformCopyData(PassOwnPtr<CrossThreadResourceRequestData> data) const { return data; }
        void doPlatformAdopt(PassOwnPtr<CrossThreadResourceRequestData>) { }

        // WebERA:

        // Notice, this value can be 0 - In some cases network requests are constructed before
        // any UI events exist. In that case we set the value to 0.
        // This value represents the event action creating this particular request
        WTF::EventActionId m_calleeEventAction;
        // This value represents the event action initiating the actual request. We can see
        // multiple senders in the lifetime of a page. We assume that senders do not overlap.
        WTF::EventActionId m_senderEventAction;
    };

    struct CrossThreadResourceRequestData : public CrossThreadResourceRequestDataBase {
    };

} // namespace WebCore

#endif // ResourceRequest_h
