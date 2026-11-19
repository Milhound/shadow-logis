#pragma once
#include <QList>
#include <QString>

namespace shadowlogis {
struct Airline {
    QString name, country, iata, icao, numeric, alias, callsign;
    bool member = false;
    bool controlledDuplicate = false;
};
// The bundled directory is a reference snapshot, not a live code-assignment service.
const QList<Airline> &airlineDirectory();
QString airlineSnapshotDate();
QString airlineSearchText(const QString &text);
bool airlineMatches(const Airline &airline, const QString &query,
                    const QString &country = {}, bool includeHistorical = false);
}
