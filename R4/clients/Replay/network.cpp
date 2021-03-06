/*
 *
 * All rights reserved.
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

#include <iostream>
#include <sstream>

#include <QDataStream>
#include <QFile>

#include <wtf/warningcollectorreport.h>

#include "fuzzyurl.h"

#include "network.h"

QNetworkReplyControllableReplay::QNetworkReplyControllableReplay(WebCore::QNetworkReplyControllableFactory* factory, QNetworkReply* reply, WebCore::QNetworkReplyInitialSnapshot* snapshot, QObject* parent)
    : QNetworkReplyControllable(factory, reply, snapshot, parent)
{

    WebCore::QNetworkReplyInitialSnapshot::QNetworkReplySnapshotEntry entry;
    QList<WebCore::QNetworkReplyInitialSnapshot::QNetworkReplySnapshotEntry>* snapshots = snapshot->getSnapshotsCopy();

    entry = snapshots->takeFirst(); // remove the current snapshot

    while (!snapshots->empty()) {
        entry = snapshots->takeFirst();
        enqueueSnapshot(entry.first, entry.second);
    }

}

QNetworkReplyControllableFactoryReplay::QNetworkReplyControllableFactoryReplay(QString logNetworkPath)
    : QNetworkReplyControllableFactory()
    , m_mode(STRICT)
{
    QFile fp(logNetworkPath);
    fp.open(QIODevice::ReadOnly);

    while (!fp.atEnd()) {
        WebCore::QNetworkReplyInitialSnapshot* snapshot = WebCore::QNetworkReplyInitialSnapshot::deserialize(&fp);

        QString url = snapshot->getUrl().toString();

        SnapshotMap::iterator iter = m_snapshots.find(url);
        if (iter == m_snapshots.end()) {
            SnapshotList* list = new SnapshotList();
            list->append(snapshot);
            m_snapshots.insert(url, list);
        } else {
            (*iter)->append(snapshot);
        }

    }

    fp.close();
}

WebCore::QNetworkReplyControllable* QNetworkReplyControllableFactoryReplay::construct(QNetworkReply* reply, QObject* parent)
{
    if (m_mode == STOP) {
        return new WebCore::QNetworkReplyControllableLive(this, reply, parent);
    }

    SnapshotMap::const_iterator iter = m_snapshots.find(reply->url().toString());

    if (iter != m_snapshots.end()) {
        // HIT

        // It could be that we have replayed all known instances of this URL
        // If that is the case let it flow through the relaxedReplayMode logic or error out
        if (!(*iter)->isEmpty()) {
            return new QNetworkReplyControllableReplay(this, reply, (*iter)->takeFirst(), parent);
        }
    }

    // Fuzzy matching

    if (m_mode == BEST_EFFORT || m_mode == BEST_EFFORT_NOND) {

        std::cout << "Warning: No exact match for URL (" << reply->url().toString().toStdString() << ") found, fuzzy matching" << std::endl;

        FuzzyUrlMatcher matcher(reply->url());

        unsigned int bestScore = 0;
        WebCore::QNetworkReplyInitialSnapshot* bestSnapshot = NULL;
        SnapshotMap::const_iterator bestList;

        iter = m_snapshots.begin();
        for (; iter != m_snapshots.end(); iter++) {

            if ((*iter)->isEmpty()) {
                continue; // skip emtpy lists
            }

            WebCore::QNetworkReplyInitialSnapshot* snapshot = (*iter)->first();

            unsigned int score = matcher.score(snapshot->getUrl());

            if (score > bestScore) {
                bestScore = score;
                bestSnapshot = snapshot;
                bestList = iter;
            }
        }

        if (bestSnapshot != NULL) {
            // We found a fuzzy match

            std::stringstream details;
            details << "Network request " << reply->url().toString().toStdString() << " fuzzy matched with " << bestSnapshot->getUrl().toString().toStdString() << ".";

            WTF::WarningCollectorReport("WEBERA_NETWORK_DATA", "Network message fuzzy matched in best effort mode.", details.str());

            std::cout << "Fuzzy match found (" << bestSnapshot->getUrl().toString().toStdString() << ")" << std::endl;

            (*bestList)->pop_front();
            return new QNetworkReplyControllableReplay(this, reply, bestSnapshot, parent);
        }

        std::stringstream details;
        details << "Network request " << reply->url().toString().toStdString() << ".";

        WTF::WarningCollectorReport("WEBERA_NETWORK_DATA", "New network request in best effort mode.", details.str());

        std::cout << "Fuzzy match not found, using a live connection (relaxed mode)" << std::endl;
        return new WebCore::QNetworkReplyControllableLive(this, reply, parent);
    }

    std::cerr << "Error: unknown network request (" << reply->url().toString().toStdString() << ") (exact mode)." << std::endl;

    SnapshotMap::iterator it = m_snapshots.begin();
    while (it != m_snapshots.end()) {

        std::cerr << it.key().toAscii().data() << std::endl;

        ++it;
    }


    std::exit(1);

}
