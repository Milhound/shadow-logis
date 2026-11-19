#include "airlines.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QResource>
#include <algorithm>
#include <stdexcept>

static void initializeAirlineResources() { Q_INIT_RESOURCE(airlines); }
namespace shadowlogis {
namespace {
QJsonObject directoryData() {
    initializeAirlineResources();
    QFile file(":/shadow-logis/reference/airlines.json");
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open the bundled airline directory.");
    QJsonParseError error;
    auto data = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !data.isObject())
        throw std::runtime_error("The bundled airline directory is invalid.");
    return data.object();
}
QString countryAlias(QString text) {
    const auto key = airlineSearchText(text);
    if (QStringList{"us", "usa", "u s a", "united states of america"}.contains(key)) return "united states";
    if (QStringList{"uk", "gb", "gbr", "great britain", "britain"}.contains(key)) return "united kingdom";
    if (key == "uae") return "united arab emirates";
    if (key == "republic of korea") return "south korea";
    if (key == "vietnam") return "viet nam";
    if (key == "turkey") return "turkiye";
    if (key == "taiwan") return "chinese taipei";
    if (key == "czech republic") return "czechia";
    if (key == "macau") return "macao";
    return key;
}
}
const QList<Airline> &airlineDirectory() {
    static const auto rows = [] {
        QList<Airline> result;
        for (const auto &value : directoryData().value("airlines").toArray()) {
            const auto row = value.toObject();
            Airline a{row["name"].toString(),row["country"].toString(),row["iata"].toString(),
                      row["icao"].toString(),row["numeric"].toString(),row["alias"].toString(),row["callsign"].toString(),
                      row["member"].toBool(),row["controlled_duplicate"].toBool()};
            if (a.name.isEmpty() || (a.iata.isEmpty() && a.icao.isEmpty())) continue;
            result.append(a);
        }
        std::stable_sort(result.begin(), result.end(), [](const Airline &a, const Airline &b) {
            return QString::localeAwareCompare(a.name,b.name) < 0;
        });
        if (result.isEmpty()) throw std::runtime_error("The bundled airline directory is empty.");
        return result;
    }();
    return rows;
}
QString airlineSnapshotDate() {
    static const auto date = directoryData().value("retrieved").toString();
    return date;
}
QString airlineSearchText(const QString &text) {
    QString result;
    for (const auto c : text.normalized(QString::NormalizationForm_KD).toCaseFolded())
        if (c.category() != QChar::Mark_NonSpacing) result += c.isLetterOrNumber() ? c : QChar(' ');
    return result.simplified();
}
bool airlineMatches(const Airline &a, const QString &query, const QString &country, bool historical) {
    if (!historical && !a.member) return false;
    const auto home = countryAlias(a.country);
    if (!country.trimmed().isEmpty() && !home.contains(countryAlias(country))) return false;
    const auto haystack = airlineSearchText(a.name+' '+a.country+' '+a.iata+' '+a.icao+' '+a.numeric+' '+a.alias+' '+a.callsign)+' '+home;
    const auto search = countryAlias(query);
    for (const auto &token : search.split(' ', Qt::SkipEmptyParts)) if (!haystack.contains(token)) return false;
    return true;
}
}
