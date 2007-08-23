#include <qapplication.h>
#include <qdom.h>
#include <qfile.h>
#include <qstring.h>
#include <qregexp.h>
#include <qstringlist.h>
#include <qvaluelist.h>
#include <qmap.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qfile.h>
#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <qurl.h>
#include <qprocess.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cerrno>

// libmyth headers
#include "exitcodes.h"
#include "mythcontext.h"
#include "mythdbcon.h"
#include "util.h"

// libmythtv headers
#include "scheduledrecording.h"
#include "datadirect.h"
#include "channelutil.h"
#include "programinfo.h"
#include "frequencytables.h"
#include "cardutil.h"
#include "sourceutil.h"
#include "remoteutil.h"
#include "videosource.h" // for is_grabber..

using namespace std;

static QString SetupIconCacheDirectory();

bool interactive = false;
bool channel_preset = false;
bool non_us_updating = false;
bool from_file = false;
bool quiet = false;
bool no_delete = false;
bool isNorthAmerica = false;
bool isJapan = false;
bool interrupted = false;
bool endofdata = false;
bool refresh_today = false;
bool refresh_tomorrow = true;
bool refresh_second = false;
bool refresh_all = false;
bool refresh_tba = true;
bool dd_grab_all = false;
bool dddataretrieved = false;
bool mark_repeats = true;
bool channel_updates = false;
bool channel_update_run = false;
bool remove_new_channels = false;
bool filter_new_channels = true;
bool only_update_channels = false;
bool need_post_grab_proc = true;
QString logged_in = "";
int raw_lineup = 0;

int maxDays = 0;
int listing_wrap_offset = 0;

QString lastdduserid;
DataDirectProcessor ddprocessor;
QString graboptions = "";
QString cardtype = QString::null;

class ChanInfo
{
  public:
    ChanInfo() { }
    ChanInfo(const ChanInfo &other) { callsign = other.callsign; 
                                      iconpath = other.iconpath;
                                      chanstr = other.chanstr;
                                      xmltvid = other.xmltvid;
                                      old_xmltvid = other.old_xmltvid;
                                      name = other.name;
                                      freqid = other.freqid;
                                      finetune = other.finetune;
                                      tvformat = other.tvformat;
                                    }

    QString callsign;
    QString iconpath;
    QString chanstr;
    QString xmltvid;
    QString old_xmltvid;
    QString name;
    QString freqid;
    QString finetune;
    QString tvformat;
};

struct ProgRating
{
    QString system;
    QString rating;
};

struct ProgCredit
{
    QString role;
    QString name;
};

class ProgInfo
{
  public:
    ProgInfo() { }
    ProgInfo(const ProgInfo &other) { channel = other.channel;
                                      startts = other.startts;
                                      endts = other.endts;
                                      start = other.start;
                                      end = other.end;
                                      title = other.title;
                                      subtitle = other.subtitle;
                                      desc = other.desc;
                                      category = other.category;
                                      catType = other.catType;
                                      airdate = other.airdate;
                                      stars = other.stars;
                                      previouslyshown = other.previouslyshown;
                                      title_pronounce = other.title_pronounce;
                                      stereo = other.stereo;
                                      subtitled = other.subtitled;
                                      hdtv = other.hdtv;
                                      closecaptioned = other.closecaptioned;
                                      partnumber = other.partnumber;
                                      parttotal = other.parttotal;
                                      seriesid = other.seriesid;
                                      originalairdate = other.originalairdate;
                                      showtype = other.showtype;
                                      colorcode = other.colorcode;
                                      syndicatedepisodenumber = other.syndicatedepisodenumber;
                                      programid = other.programid;
        
                                      clumpidx = other.clumpidx;
                                      clumpmax = other.clumpmax;
                                      ratings = other.ratings;
                                      credits = other.credits;
                                      content = other.content;
                                    }


    QString channel;
    QString startts;
    QString endts;
    QDateTime start;
    QDateTime end;
    QString title;
    QString subtitle;
    QString desc;
    QString category;
    QString catType;
    QString airdate;
    QString stars;
    bool previouslyshown;
    QString title_pronounce;
    bool stereo;
    bool subtitled;
    bool hdtv;
    bool closecaptioned;
    QString partnumber;
    QString parttotal;
    QString seriesid;                                
    QString originalairdate;
    QString showtype;
    QString colorcode;
    QString syndicatedepisodenumber;
    QString programid;

    QString clumpidx;
    QString clumpmax;
    QValueList<ProgRating> ratings;
    QValueList<ProgCredit> credits;
    QString content;
};

bool operator<(const ProgInfo &a, const ProgInfo &b)
{
    return (a.start < b.start);
}

bool operator>(const ProgInfo &a, const ProgInfo &b)
{
    return (a.start > b.start);
}

bool operator<=(const ProgInfo &a, const ProgInfo &b)
{
    return (a.start <= b.start);
}

struct Source
{
    int id;
    QString name;
    QString xmltvgrabber;
    QString userid;
    QString password;
    QString lineupid;
    bool    xmltvgrabber_baseline;
    bool    xmltvgrabber_manualconfig;
    bool    xmltvgrabber_cache;
    QString xmltvgrabber_prefmethod;
    vector<int> dd_dups;
};


unsigned int ELFHash(const char *s)
{
    /* ELF hash uses unsigned chars and unsigned arithmetic for portability */
    const unsigned char *name = (const unsigned char *)s;
    unsigned long h = 0, g;

    while (*name)
    { /* do some fancy bitwanking on the string */
        h = (h << 4) + (unsigned long)(*name++);
        if ((g = (h & 0xF0000000UL))!=0)
            h ^= (g >> 24);
        h &= ~g;

    }

    return (int)h;
}

void clearDataByChannel(int chanid, QDateTime from, QDateTime to) 
{
    int secs;
    QDateTime newFrom, newTo;

    MSqlQuery query(MSqlQuery::InitCon());

    query.prepare("SELECT tmoffset FROM channel where chanid = :CHANID ;");
    query.bindValue(":CHANID", chanid);
    query.exec();
    if (!query.isActive() || query.size() != 1)
    {
        MythContext::DBError("clearDataByChannel", query);
        return;
    }
    query.next();
    secs = query.value(0).toInt();

    secs *= 60;
    newFrom = from.addSecs(secs);
    newTo = to.addSecs(secs);

    query.prepare("DELETE FROM program "
                  "WHERE starttime >= :FROM AND starttime < :TO "
                  "AND chanid = :CHANID ;");
    query.bindValue(":FROM", newFrom);
    query.bindValue(":TO", newTo);
    query.bindValue(":CHANID", chanid);
    query.exec();

    query.prepare("DELETE FROM programrating "
                  "WHERE starttime >= :FROM AND starttime < :TO "
                  "AND chanid = :CHANID ;");
    query.bindValue(":FROM", newFrom);
    query.bindValue(":TO", newTo);
    query.bindValue(":CHANID", chanid);
    query.exec();

    query.prepare("DELETE FROM credits "
                  "WHERE starttime >= :FROM AND starttime < :TO "
                  "AND chanid = :CHANID ;");
    query.bindValue(":FROM", newFrom);
    query.bindValue(":TO", newTo);
    query.bindValue(":CHANID", chanid);
    query.exec();

    query.prepare("DELETE FROM programgenres "
                  "WHERE starttime >= :FROM AND starttime < :TO "
                  "AND chanid = :CHANID ;");
    query.bindValue(":FROM", newFrom);
    query.bindValue(":TO", newTo);
    query.bindValue(":CHANID", chanid);
    query.exec();
}

void clearDataBySource(int sourceid, QDateTime from, QDateTime to) 
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT chanid FROM channel WHERE "
                  "sourceid = :SOURCE ;");
    query.bindValue(":SOURCE", sourceid);

    if (!query.exec())
        MythContext::DBError("Selecting channels per source", query);
        
    if (query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            int chanid = query.value(0).toInt();
            clearDataByChannel(chanid, from, to);
        }
    }
}

// icon mapping stuff
namespace {
const char * const IM_DOC_TAG = "iconmappings";

const char * const IM_CS_TO_NET_TAG = "callsigntonetwork";
const char * const IM_CS_TAG = "callsign";

const char * const IM_NET_TAG = "network";

const char * const IM_NET_TO_URL_TAG = "networktourl";
const char * const IM_NET_URL_TAG = "url";

const char * const BASEURLMAP_START = "mythfilldatabase.urlmap.";

const char * const IM_BASEURL_TAG = "baseurl";
const char * const IM_BASE_STUB_TAG = "stub";

QString expandURLString(const QString &url)
{
    QRegExp expandtarget("\\[([^\\]]+)\\]");
    QString retval = url;

    int found_at = 0;
    int start_index = 0;
    while (found_at != -1)
    {
        found_at = expandtarget.search(retval, start_index);
        if (found_at != -1)
        {
            QString no_mapping("no_URL_mapping");
            QString search_string = expandtarget.cap(1);
            QString expanded_text = gContext->GetSetting(
                    QString(BASEURLMAP_START) + search_string, no_mapping);
            if (expanded_text != no_mapping)
            {
                retval.replace(found_at, expandtarget.matchedLength(),
                        expanded_text);
            }
            else
            {
                start_index = found_at + expandtarget.matchedLength();
            }
        }
    }

    return retval;
}

void UpdateSourceIcons(int sourceid)
{
    VERBOSE(VB_GENERAL,
            QString("Updating icons for sourceid: %1").arg(sourceid));

    QString fileprefix = SetupIconCacheDirectory();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT ch.chanid, nim.url "
            "FROM (channel ch, callsignnetworkmap csm) "
            "RIGHT JOIN networkiconmap nim ON csm.network = nim.network "
            "WHERE ch.callsign = csm.callsign AND "
            "(icon = :NOICON OR icon = '') AND ch.sourceid = :SOURCEID");
    query.bindValue(":SOURCEID", sourceid);
    query.bindValue(":NOICON", "none");

    if (!query.exec())
        MythContext::DBError("Looking for icons to fetch", query);

    if (query.isActive() && query.size() > 0)
    {
        while (query.next())
        {
            QString icon_url = expandURLString(query.value(1).toString());
            QFileInfo qfi(icon_url);
            QFile localfile(fileprefix + "/" + qfi.fileName());
            if (!localfile.exists())
            {
                QString icon_get_command =
                    QString("wget --timestamping --directory-prefix=%1 '%2'")
                            .arg(fileprefix).arg(icon_url);

                if ((print_verbose_messages & VB_GENERAL) == 0)
                    icon_get_command += " > /dev/null 2> /dev/null";
                VERBOSE(VB_GENERAL,
                        QString("Attempting to fetch icon with: %1")
                                .arg(icon_get_command));

                system(icon_get_command);
            }

            if (localfile.exists())
            {
                int chanid = query.value(0).toInt();
                if (!quiet)
                {
                    QString m = QString("Updating channel icon for chanid: %1")
                        .arg(chanid);
                    cout << m << endl;
                }
                MSqlQuery icon_update_query(MSqlQuery::InitCon());
                icon_update_query.prepare("UPDATE channel SET icon = :ICON "
                        "WHERE chanid = :CHANID AND sourceid = :SOURCEID");
                icon_update_query.bindValue(":ICON", localfile.name());
                icon_update_query.bindValue(":CHANID", query.value(0).toInt());
                icon_update_query.bindValue(":SOURCEID", sourceid);

                if (!icon_update_query.exec())
                    MythContext::DBError("Setting the icon file name",
                            icon_update_query);
            }
            else
            {
                cerr << QString(
                        "Error retrieving icon from '%1' to file '%2'")
                        .arg(icon_url)
                        .arg(localfile.name())
                     << endl;
            }
        }
    }
}

bool dash_open(QFile &file, const QString &filename, int m, FILE *handle = NULL)
{
    bool retval = false;
    if (filename == "-")
    {
        if (handle == NULL)
        {
            handle = stdout;
            if (m & IO_ReadOnly)
            {
                handle = stdin;
            }
        }
        retval = file.open(m, handle);
    }
    else
    {
        file.setName(filename);
        retval = file.open(m);
    }

    return retval;
}

class DOMException
{
  private:
    QString message;

  protected:
    void setMessage(const QString &mes)
    {
        message = mes;
    }

  public:
    DOMException() : message("Unknown DOMException") {}
    virtual ~DOMException() {}
    DOMException(const QString &mes) : message(mes) {}
    QString getMessage()
    {
        return message;
    }
};

class DOMBadElementConversion : public DOMException
{
  public:
    DOMBadElementConversion()
    {
        setMessage("Unknown DOMBadElementConversion");
    }
    DOMBadElementConversion(const QString &mes) : DOMException(mes) {}
    DOMBadElementConversion(const QDomNode &node)
    {
        setMessage(QString("Unable to convert node: '%1' to QDomElement.")
                .arg(node.nodeName()));
    }
};

class DOMUnknownChildElement : public DOMException
{
  public:
    DOMUnknownChildElement()
    {
        setMessage("Unknown DOMUnknownChildElement");
    }
    DOMUnknownChildElement(const QString &mes) : DOMException(mes) {}
    DOMUnknownChildElement(const QDomElement &e, QString child_name)
    {
        setMessage(QString("Unknown child element '%1' of: '%2'")
                .arg(child_name)
                .arg(e.tagName()));
    }
};

QDomElement nodeToElement(QDomNode &node)
{
    QDomElement retval = node.toElement();
    if (retval.isNull())
    {
        throw DOMBadElementConversion(node);
    }
    return retval;
}

QString getNamedElementText(const QDomElement &e,
        const QString &child_element_name)
{
    QDomNode child_node = e.namedItem(child_element_name);
    if (child_node.isNull())
    {
        throw DOMUnknownChildElement(e, child_element_name);
    }
    QDomElement element = nodeToElement(child_node);
    return element.text();
}

void ImportIconMap(const QString &filename)
{
    if (!quiet)
    {
        QString msg = QString("Importing icon mapping from %1...")
                .arg(filename);
        cout << msg << endl;
    }
    QFile xml_file;

    if (dash_open(xml_file, filename, IO_ReadOnly))
    {
        QDomDocument doc;
        QString de_msg;
        int de_ln = 0;
        int de_column = 0;
        if (doc.setContent(&xml_file, false, &de_msg, &de_ln, &de_column))
        {
            MSqlQuery nm_query(MSqlQuery::InitCon());
            nm_query.prepare("REPLACE INTO networkiconmap(network, url) "
                    "VALUES(:NETWORK, :URL)");
            MSqlQuery cm_query(MSqlQuery::InitCon());
            cm_query.prepare("REPLACE INTO callsignnetworkmap(callsign, "
                    "network) VALUES(:CALLSIGN, :NETWORK)");
            MSqlQuery su_query(MSqlQuery::InitCon());
            su_query.prepare("UPDATE settings SET data = :URL "
                    "WHERE value = :STUBNAME");
            MSqlQuery si_query(MSqlQuery::InitCon());
            si_query.prepare("INSERT INTO settings(value, data) "
                    "VALUES(:STUBNAME, :URL)");

            QDomElement element = doc.documentElement();

            QDomNode node = element.firstChild();
            while (!node.isNull())
            {
                try
                {
                    QDomElement e = nodeToElement(node);
                    if (e.tagName() == IM_NET_TO_URL_TAG)
                    {
                        QString net = getNamedElementText(e, IM_NET_TAG);
                        QString u = getNamedElementText(e, IM_NET_URL_TAG);

                        nm_query.bindValue(":NETWORK", net.stripWhiteSpace());
                        nm_query.bindValue(":URL", u.stripWhiteSpace());
                        if (!nm_query.exec())
                            MythContext::DBError(
                                    "Inserting network->url mapping", nm_query);
                    }
                    else if (e.tagName() == IM_CS_TO_NET_TAG)
                    {
                        QString cs = getNamedElementText(e, IM_CS_TAG);
                        QString net = getNamedElementText(e, IM_NET_TAG);

                        cm_query.bindValue(":CALLSIGN", cs.stripWhiteSpace());
                        cm_query.bindValue(":NETWORK", net.stripWhiteSpace());
                        if (!cm_query.exec())
                            MythContext::DBError("Inserting callsign->network "
                                    "mapping", cm_query);
                    }
                    else if (e.tagName() == IM_BASEURL_TAG)
                    {
                        MSqlQuery *qr = &si_query;

                        QString st(BASEURLMAP_START);
                        st += getNamedElementText(e, IM_BASE_STUB_TAG);
                        QString u = getNamedElementText(e, IM_NET_URL_TAG);

                        MSqlQuery qc(MSqlQuery::InitCon());
                        qc.prepare("SELECT COUNT(*) FROM settings "
                                "WHERE value = :STUBNAME");
                        qc.bindValue(":STUBNAME", st);
                        qc.exec();
                        if (qc.isActive() && qc.size() > 0)
                        {
                            qc.first();
                            if (qc.value(0).toInt() != 0)
                            {
                                qr = &su_query;
                            }
                        }

                        qr->bindValue(":STUBNAME", st);
                        qr->bindValue(":URL", u);

                        if (!qr->exec())
                            MythContext::DBError(
                                    "Inserting callsign->network mapping", *qr);
                    }
                }
                catch (DOMException &e)
                {
                    cerr << QString("Error while processing %1: %2")
                            .arg(node.nodeName())
                            .arg(e.getMessage())
                         << endl;
                }
                node = node.nextSibling();
            }
        }
        else
        {
            cerr << QString(
                    "Error unable to set document content: %1:%2c%3 %4")
                    .arg(filename)
                    .arg(de_ln)
                    .arg(de_column)
                    .arg(de_msg)
                 << endl;
        }
    }
    else
    {
        cerr << QString("Error unable to open '%1' for reading.")
                .arg(filename)
             << endl;
    }
}

void ExportIconMap(const QString &filename)
{
    if (!quiet)
    {
        cout << QString("Exporting icon mapping to %1...").arg(filename)
             << endl;
    }
    QFile xml_file(filename);
    if (dash_open(xml_file, filename, IO_WriteOnly))
    {
        QTextStream os(&xml_file);
        os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        os << "<!-- generated by mythfilldatabase -->\n";

        QDomDocument iconmap;
        QDomElement roote = iconmap.createElement(IM_DOC_TAG);

        MSqlQuery query(MSqlQuery::InitCon());
        query.exec("SELECT * FROM callsignnetworkmap ORDER BY callsign;");

        if (query.isActive() && query.size() > 0)
        {
            while (query.next())
            {
                QDomElement cs2nettag = iconmap.createElement(IM_CS_TO_NET_TAG);
                QDomElement cstag = iconmap.createElement(IM_CS_TAG);
                QDomElement nettag = iconmap.createElement(IM_NET_TAG);
                QDomText cs_text = iconmap.createTextNode(
                        query.value(1).toString());
                QDomText net_text = iconmap.createTextNode(
                        query.value(2).toString());

                cstag.appendChild(cs_text);
                nettag.appendChild(net_text);

                cs2nettag.appendChild(cstag);
                cs2nettag.appendChild(nettag);

                roote.appendChild(cs2nettag);
            }
        }

        query.exec("SELECT * FROM networkiconmap ORDER BY network;");
        if (query.isActive() && query.size() > 0)
        {
            while (query.next())
            {
                QDomElement net2urltag = iconmap.createElement(
                        IM_NET_TO_URL_TAG);
                QDomElement nettag = iconmap.createElement(IM_NET_TAG);
                QDomElement urltag = iconmap.createElement(IM_NET_URL_TAG);
                QDomText net_text = iconmap.createTextNode(
                        query.value(1).toString());
                QDomText url_text = iconmap.createTextNode(
                        query.value(2).toString());

                nettag.appendChild(net_text);
                urltag.appendChild(url_text);

                net2urltag.appendChild(nettag);
                net2urltag.appendChild(urltag);

                roote.appendChild(net2urltag);
            }
        }

        query.prepare("SELECT value,data FROM settings WHERE value "
                "LIKE :URLMAP");
        query.bindValue(":URLMAP", QString(BASEURLMAP_START) + "%");
        query.exec();
        if (query.isActive() && query.size() > 0)
        {
            QRegExp baseax("\\.([^\\.]+)$");
            while (query.next())
            {
                QString base_stub = query.value(0).toString();
                if (baseax.search(base_stub) != -1)
                {
                    base_stub = baseax.cap(1);
                }

                QDomElement baseurltag = iconmap.createElement(IM_BASEURL_TAG);
                QDomElement stubtag = iconmap.createElement(
                        IM_BASE_STUB_TAG);
                QDomElement urltag = iconmap.createElement(IM_NET_URL_TAG);
                QDomText base_text = iconmap.createTextNode(base_stub);
                QDomText url_text = iconmap.createTextNode(
                        query.value(1).toString());

                stubtag.appendChild(base_text);
                urltag.appendChild(url_text);

                baseurltag.appendChild(stubtag);
                baseurltag.appendChild(urltag);

                roote.appendChild(baseurltag);
            }
        }

        iconmap.appendChild(roote);
        iconmap.save(os, 4);
    }
    else
    {
        cerr << QString("Error unable to open '%1' for writing.") << endl;
    }
}

void RunSimpleQuery(const QString &query)
{
    MSqlQuery q(MSqlQuery::InitCon());
    if (!q.exec(query))
        MythContext::DBError("RunSimpleQuery ", q);
}

void ResetIconMap(bool reset_icons)
{
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("DELETE FROM settings WHERE value LIKE :URLMAPLIKE");
    query.bindValue(":URLMAPLIKE", QString(BASEURLMAP_START) + '%');
    if (!query.exec())
        MythContext::DBError("ResetIconMap", query);

    RunSimpleQuery("TRUNCATE TABLE callsignnetworkmap;");
    RunSimpleQuery("TRUNCATE TABLE networkiconmap");

    if (reset_icons)
    {
        RunSimpleQuery("UPDATE channel SET icon = 'none'");
    }
}

} // namespace

void get_atsc_stuff(QString channum, int sourceid, int freqid,
                    int &major, int &minor, long long &freq)
{
    major = freqid;
    minor = 0;

    int chansep = channum.find(QRegExp("\\D"));
    if (chansep < 0)
        return;

    major = channum.left(chansep).toInt();
    minor = channum.right(channum.length() - (chansep + 1)).toInt();

    freq = get_center_frequency("atsc", "vsb8", "us", freqid);

    // Check if this is connected to an HDTV card.
    MSqlQuery query(MSqlQuery::DDCon());
    query.prepare(
        "SELECT cardtype "
        "FROM capturecard, cardinput "
        "WHERE cardinput.cardid = capturecard.cardid AND "
        "      sourceid         = :SOURCEID");
    query.bindValue(":SOURCEID", sourceid);

    if (query.exec() && query.isActive() && query.next() &&
        query.value(0).toString() == "HDTV")
    {
        freq -= 1750000; // convert to visual carrier freq.
    }
}

static bool insert_chan(uint sourceid)
{
    bool insert_channels = channel_updates;
    if (!insert_channels)
    {
        bool isEncoder, isUnscanable;
        if (cardtype.isEmpty())
        {
            isEncoder    = SourceUtil::IsEncoder(sourceid);
            isUnscanable = SourceUtil::IsUnscanable(sourceid);
        }
        else
        {
            isEncoder    = CardUtil::IsEncoder(cardtype);
            isUnscanable = CardUtil::IsUnscanable(cardtype);
        }
        insert_channels = (isEncoder || isUnscanable) && !remove_new_channels;
    }

    return insert_channels;
}

// DataDirect stuff
void DataDirectStationUpdate(Source source, bool update_icons = true)
{
    DataDirectProcessor::UpdateStationViewTable(source.lineupid);

    bool insert_channels = insert_chan(source.id);
    int new_channels = DataDirectProcessor::UpdateChannelsSafe(
        source.id, insert_channels, filter_new_channels);

    //  User must pass "--do-channel-updates" for these updates
    if (channel_updates)
    {
        DataDirectProcessor::UpdateChannelsUnsafe(
            source.id, filter_new_channels);
    }
    // TODO delete any channels which no longer exist in listings source

    if (update_icons)
        UpdateSourceIcons(source.id);

    // Unselect channels not in users lineup for DVB, HDTV
    if (!insert_channels && (new_channels > 0) &&
        is_grabber_labs(source.xmltvgrabber))
    {
        bool ok0 = (logged_in == source.userid);
        bool ok1 = (raw_lineup == source.id);
        if (!ok0)
        {
            VERBOSE(VB_GENERAL, "Grabbing login cookies for listing update");
            ok0 = ddprocessor.GrabLoginCookiesAndLineups();
        }
        if (ok0 && !ok1)
        {
            VERBOSE(VB_GENERAL, "Grabbing listing for listing update");
            ok1 = ddprocessor.GrabLineupForModify(source.lineupid);
        }
        if (ok1)
        {
            ddprocessor.UpdateListings(source.id);
            VERBOSE(VB_GENERAL, QString("Removed %1 channel(s) from lineup.")
                    .arg(new_channels));
        }
    }
}

bool DataDirectUpdateChannels(Source source)
{
    if (source.xmltvgrabber == "datadirect")
        ddprocessor.SetListingsProvider(DD_ZAP2IT);
    else if (source.xmltvgrabber == "schedulesdirect1")
        ddprocessor.SetListingsProvider(DD_SCHEDULES_DIRECT);
    else
    {
        VERBOSE(VB_IMPORTANT,
                "FillData: We only support DataDirectUpdateChannels with "
                "TMS Labs and Schedules Direct.");
        return false;
    }

    ddprocessor.SetUserID(source.userid);
    ddprocessor.SetPassword(source.password);

    bool ok = true;
    if (!is_grabber_labs(source.xmltvgrabber))
    {
        ok = ddprocessor.GrabLineupsOnly();
    }
    else
    {
        ok = ddprocessor.GrabFullLineup(source.lineupid, true,
                                        insert_chan(source.id)/*only sel*/);
        logged_in  = source.userid;
        raw_lineup = source.id;
    }

    if (ok)
        DataDirectStationUpdate(source, false);

    return ok;
}

void DataDirectProgramUpdate() 
{
    MSqlQuery query(MSqlQuery::DDCon());

    //cerr << "Adding rows to main program table from view table..\n";
    if (!query.exec("INSERT IGNORE INTO program (chanid, starttime, endtime, "
                    "title, subtitle, description, "
                    "showtype, category, category_type, "
                    "airdate, stars, previouslyshown, stereo, subtitled, "
                    "hdtv, closecaptioned, partnumber, parttotal, seriesid, "
                    "originalairdate, colorcode, syndicatedepisodenumber, "
                    "programid) "
                    "SELECT dd_v_program.chanid, "
                    "DATE_ADD(starttime, INTERVAL channel.tmoffset MINUTE), "
                    "DATE_ADD(endtime, INTERVAL channel.tmoffset MINUTE), "
                    "title, subtitle, description, "
                    "showtype, dd_genre.class, category_type, "
                    "airdate, stars, previouslyshown, stereo, subtitled, "
                    "hdtv, closecaptioned, partnumber, parttotal, seriesid, "
                    "originalairdate, colorcode, syndicatedepisodenumber, "
                    "dd_v_program.programid FROM (dd_v_program, channel) "
                    "LEFT JOIN dd_genre ON ("
                    "dd_v_program.programid = dd_genre.programid AND "
                    "dd_genre.relevance = '0') "
                    "WHERE dd_v_program.chanid = channel.chanid;"))
        MythContext::DBError("Inserting into program table", query);

    //cerr << "Finished adding rows to main program table...\n";
    //cerr << "Adding program ratings...\n";

    if (!query.exec("INSERT IGNORE INTO programrating (chanid, starttime, "
                    "system, rating) SELECT dd_v_program.chanid, "
                    "DATE_ADD(starttime, INTERVAL channel.tmoffset MINUTE), "
                    " 'MPAA', "
                    "mpaarating FROM dd_v_program, channel WHERE "
                    "mpaarating != '' AND dd_v_program.chanid = "
                    "channel.chanid"))
        MythContext::DBError("Inserting into programrating table", query);

    if (!query.exec("INSERT IGNORE INTO programrating (chanid, starttime, "
                    "system, rating) SELECT dd_v_program.chanid, "
                    "DATE_ADD(starttime, INTERVAL channel.tmoffset MINUTE), "
                    "'VCHIP', "
                    "tvrating FROM dd_v_program, channel WHERE tvrating != ''"
                    " AND dd_v_program.chanid = channel.chanid"))
        MythContext::DBError("Inserting into programrating table", query);

    //cerr << "Finished adding program ratings...\n";
    //cerr << "Populating people table from production crew list...\n";

    if (!query.exec("INSERT IGNORE INTO people (name) SELECT fullname "
                    "FROM dd_productioncrew;"))
        MythContext::DBError("Inserting into people table", query);

    //cerr << "Finished adding people...\n";
    //cerr << "Adding credits entries from production crew list...\n";

    if (!query.exec("INSERT IGNORE INTO credits (chanid, starttime, person, "
                    "role) SELECT dd_v_program.chanid, "
                    "DATE_ADD(starttime, INTERVAL channel.tmoffset MINUTE), "
                    "person, role "
                    "FROM dd_productioncrew, dd_v_program, channel, people "
                    "WHERE "
                    "((dd_productioncrew.programid = dd_v_program.programid) "
                    "AND (dd_productioncrew.fullname = people.name)) "
                    "AND dd_v_program.chanid = channel.chanid;"))
        MythContext::DBError("Inserting into credits table", query);

    //cerr << "Finished inserting credits...\n";
    //cerr << "Adding genres...\n";

    if (!query.exec("INSERT IGNORE INTO programgenres (chanid, starttime, "
                    "relevance, genre) SELECT dd_v_program.chanid, "
                    "DATE_ADD(starttime, INTERVAL channel.tmoffset MINUTE), "
                    "relevance, class FROM dd_v_program, dd_genre, channel "
                    "WHERE (dd_v_program.programid = dd_genre.programid) "
                    "AND dd_v_program.chanid = channel.chanid"))
        MythContext::DBError("Inserting into programgenres table",query);

    //cerr << "Done...\n";
}

bool grabDDData(Source source, int poffset, QDate pdate, int ddSource) 
{
    if (source.dd_dups.empty())
        ddprocessor.SetCacheData(false);
    else
    {
        VERBOSE(VB_GENERAL, QString(
                    "This DataDirect listings source is "
                    "shared by %1 MythTV lineups")
                .arg(source.dd_dups.size()+1));
        if (source.id > source.dd_dups[0])
        {
            VERBOSE(VB_IMPORTANT, "We should use cached data for this one");
        }
        else if (source.id < source.dd_dups[0])
        {
            VERBOSE(VB_IMPORTANT, "We should keep data around after this one");
        }
        ddprocessor.SetCacheData(true);
    }

    ddprocessor.SetListingsProvider(ddSource);
    ddprocessor.SetUserID(source.userid);
    ddprocessor.SetPassword(source.password);
    
    bool needtoretrieve = true;

    if (source.userid != lastdduserid)
        dddataretrieved = false;

    if (dd_grab_all && dddataretrieved)
        needtoretrieve = false;

    QDateTime qdtNow = QDateTime::currentDateTime();
    MSqlQuery query(MSqlQuery::DDCon());
    QString status = "currently running.";

    query.exec(QString("UPDATE settings SET data ='%1' "
                       "WHERE value='mythfilldatabaseLastRunStart'")
                       .arg(qdtNow.toString("yyyy-MM-dd hh:mm")));

    if (needtoretrieve)
    {
        VERBOSE(VB_GENERAL, "Retrieving datadirect data.");
        if (dd_grab_all) 
        {
            VERBOSE(VB_GENERAL, "Grabbing ALL available data.");
            if (!ddprocessor.GrabAllData())
            {
                VERBOSE(VB_IMPORTANT, "Encountered error in grabbing data.");
                return false;
            }
        }
        else
        {
            QDateTime fromdatetime = QDateTime(pdate);
            QDateTime todatetime;
            fromdatetime.setTime_t(QDateTime(pdate).toTime_t(),Qt::UTC);
            fromdatetime = fromdatetime.addDays(poffset);
            todatetime = fromdatetime.addDays(1);

            VERBOSE(VB_GENERAL, QString("Grabbing data for %1 offset %2")
                                          .arg(pdate.toString())
                                          .arg(poffset));
            VERBOSE(VB_GENERAL, QString("From %1 to %2 (UTC)")
                                          .arg(fromdatetime.toString())
                                          .arg(todatetime.toString()));

            if (!ddprocessor.GrabData(fromdatetime, todatetime))
            {
                VERBOSE(VB_IMPORTANT, "Encountered error in grabbing data.");
                return false;
            }
        }

        dddataretrieved = true;
        lastdduserid = source.userid;
    }
    else
    {
        VERBOSE(VB_GENERAL, "Using existing grabbed data in temp tables.");
    }

    VERBOSE(VB_GENERAL,
            QString("Grab complete.  Actual data from %1 to %2 (UTC)")
            .arg(ddprocessor.GetDDProgramsStartAt().toString())
            .arg(ddprocessor.GetDDProgramsEndAt().toString()));

    qdtNow = QDateTime::currentDateTime();
    query.exec(QString("UPDATE settings SET data ='%1' "
                       "WHERE value='mythfilldatabaseLastRunEnd'")
                       .arg(qdtNow.toString("yyyy-MM-dd hh:mm")));

    VERBOSE(VB_GENERAL, "Main temp tables populated.");
    if (!channel_update_run)
    {
        VERBOSE(VB_GENERAL, "Updating myth channels.");
        DataDirectStationUpdate(source);
        VERBOSE(VB_GENERAL, "Channels updated.");
        channel_update_run = true;
    }

    //cerr << "Creating program view table...\n";
    DataDirectProcessor::UpdateProgramViewTable(source.id);
    //cerr <<  "Finished creating program view table...\n";

    query.exec("SELECT count(*) from dd_v_program;");
    if (query.isActive() && query.size() > 0)
    {
        query.next();
        if (query.value(0).toInt() < 1)
        {
            VERBOSE(VB_GENERAL, "Did not find any new program data.");
            return false;
        }
    }
    else
    {
        VERBOSE(VB_GENERAL, "Failed testing program view table.");
        return false;
    }

    VERBOSE(VB_GENERAL, "Clearing data for source.");
    QDateTime fromlocaldt = ddprocessor.GetDDProgramsStartAt(true);
    QDateTime tolocaldt = ddprocessor.GetDDProgramsEndAt(true);

    VERBOSE(VB_GENERAL, QString("Clearing from %1 to %2 (localtime)")
                                  .arg(fromlocaldt.toString())
                                  .arg(tolocaldt.toString()));
    clearDataBySource(source.id, fromlocaldt,tolocaldt);
    VERBOSE(VB_GENERAL, "Data for source cleared.");
    
    VERBOSE(VB_GENERAL, "Updating programs.");
    DataDirectProgramUpdate();
    VERBOSE(VB_GENERAL, "Program table update complete.");

    return true;
}

// XMLTV stuff

QString getFirstText(QDomElement element)
{
    for (QDomNode dname = element.firstChild(); !dname.isNull();
         dname = dname.nextSibling())
    {
        QDomText t = dname.toText();
        if (!t.isNull())
            return t.data();
    }
    return "";
}

ChanInfo *parseChannel(QDomElement &element, QUrl baseUrl) 
{
    ChanInfo *chaninfo = new ChanInfo;

    QString xmltvid = element.attribute("id", "");
    QStringList split = QStringList::split(" ", xmltvid);

    bool xmltvisjunk = false;

    if (isNorthAmerica)
    {
        if (xmltvid.contains("zap2it"))
        {
            xmltvisjunk = true;
            chaninfo->chanstr = "";
            chaninfo->xmltvid = xmltvid;
            chaninfo->callsign = "";
        }
        else
        {
            chaninfo->xmltvid = split[0];
            chaninfo->chanstr = split[0];
            if (split.size() > 1)
                chaninfo->callsign = split[1];
            else
                chaninfo->callsign = "";
        }
    }
    else
    {
        chaninfo->callsign = "";
        chaninfo->chanstr = "";
        chaninfo->xmltvid = xmltvid;
    }

    chaninfo->iconpath = "";
    chaninfo->name = "";
    chaninfo->finetune = "";
    chaninfo->tvformat = "Default";

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "icon")
            {
                QUrl iconUrl(baseUrl, info.attribute("src", ""), true);
                chaninfo->iconpath = iconUrl.toString();
            }
            else if (info.tagName() == "display-name")
            {
                if (chaninfo->name.length() == 0)
                {
                    chaninfo->name = info.text();
                    if (xmltvisjunk)
                    {
                        QStringList split = QStringList::split(" ", 
                                                               chaninfo->name);
          
                        if (split[0] == "Channel")
                        { 
                            chaninfo->old_xmltvid = split[1];
                            chaninfo->chanstr = split[1];
                            if (split.size() > 2)
                                chaninfo->callsign = split[2];
                        }
                        else
                        {
                            chaninfo->old_xmltvid = split[0];
                            chaninfo->chanstr = split[0];
                            if (split.size() > 1)
                                chaninfo->callsign = split[1];
                        }
                    }
                }
                else if (isJapan && chaninfo->callsign.length() == 0)
                {
                    chaninfo->callsign = info.text();
                }
                else if (chaninfo->chanstr.length() == 0)
                {
                    chaninfo->chanstr = info.text();
                }
            }
        }
    }

    chaninfo->freqid = chaninfo->chanstr;
    return chaninfo;
}

int TimezoneToInt (QString timezone)
{
    // we signal an error by setting it invalid (> 840min = 14hr)
    int result = 841;
    
    if (timezone.upper() == "UTC" || timezone.upper() == "GMT")
        return 0;

    if (timezone.length() == 5)
    {
        bool ok;

        result = timezone.mid(1,2).toInt(&ok, 10);

        if (!ok)
            result = 841;
        else
        {
            result *= 60;

            int min = timezone.right(2).toInt(&ok, 10);

            if (!ok)
                result = 841;
            else
            {
                result += min;
                if (timezone.left(1) == "-")
                    result *= -1;
            }
        }
    }
    return result;
}

// localTimezoneOffset: 841 == "None", -841 == "Auto", other == fixed offset
void fromXMLTVDate(QString &timestr, QDateTime &dt, int localTimezoneOffset = 841)
{
    if (timestr.isEmpty())
    {
        cerr << "Ignoring empty timestamp." << endl;
        return;
    }

    QStringList split = QStringList::split(" ", timestr);
    QString ts = split[0];
    bool ok;
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;

    if (ts.length() == 14)
    {
        year  = ts.left(4).toInt(&ok, 10);
        month = ts.mid(4,2).toInt(&ok, 10);
        day   = ts.mid(6,2).toInt(&ok, 10);
        hour  = ts.mid(8,2).toInt(&ok, 10);
        min   = ts.mid(10,2).toInt(&ok, 10);
        sec   = ts.mid(12,2).toInt(&ok, 10);
    }
    else if (ts.length() == 12)
    {
        year  = ts.left(4).toInt(&ok, 10);
        month = ts.mid(4,2).toInt(&ok, 10);
        day   = ts.mid(6,2).toInt(&ok, 10);
        hour  = ts.mid(8,2).toInt(&ok, 10);
        min   = ts.mid(10,2).toInt(&ok, 10);
        sec   = 0;
    }
    else
    {
        cerr << "Ignoring unknown timestamp format: " << ts << endl;
        return;
    }

    dt = QDateTime(QDate(year, month, day),QTime(hour, min, sec));

    if ((split.size() > 1) && (localTimezoneOffset <= 840))
    {
        QString tmp = split[1];
        tmp.stripWhiteSpace();

        int ts_offset = TimezoneToInt(tmp);
        if (abs(ts_offset) > 840)
        {
            ts_offset = 0;
            localTimezoneOffset = 841;
        }
        dt = dt.addSecs(-ts_offset * 60);
    }

    if (localTimezoneOffset < -840)
    {
        dt = MythUTCToLocal(dt);
    }
    else if (abs(localTimezoneOffset) <= 840)
    {
        dt = dt.addSecs(localTimezoneOffset * 60 );
    }

    timestr = dt.toString("yyyyMMddhhmmss");
}

void parseCredits(QDomElement &element, ProgInfo *pginfo)
{
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            ProgCredit credit;
            credit.role = info.tagName();
            credit.name = getFirstText(info);
            pginfo->credits.append(credit);
        }
    }
}

ProgInfo *parseProgram(QDomElement &element, int localTimezoneOffset)
{
    QString uniqueid, seriesid, season, episode;
    ProgInfo *pginfo = new ProgInfo;
 
    pginfo->previouslyshown = pginfo->stereo = pginfo->subtitled =
    pginfo->hdtv = pginfo->closecaptioned = false;

    pginfo->subtitle = pginfo->title = pginfo->desc =
    pginfo->category = pginfo->content = pginfo->catType =
    pginfo->syndicatedepisodenumber =  pginfo->partnumber =
    pginfo->parttotal = pginfo->showtype = pginfo->colorcode =
    pginfo->stars = "";

    QString text = element.attribute("start", "");
    fromXMLTVDate(text, pginfo->start, localTimezoneOffset);
    pginfo->startts = text;

    text = element.attribute("stop", "");
    fromXMLTVDate(text, pginfo->end, localTimezoneOffset);
    pginfo->endts = text;

    text = element.attribute("channel", "");
    QStringList split = QStringList::split(" ", text);   
 
    pginfo->channel = split[0];

    text = element.attribute("clumpidx", "");
    if (!text.isEmpty()) 
    {
        split = QStringList::split("/", text);
        pginfo->clumpidx = split[0];
        pginfo->clumpmax = split[1];
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "title")
            {
                if (isJapan)
                {
                    if (info.attribute("lang") == "ja_JP")
                    {
                        pginfo->title = getFirstText(info);
                    }
                    else if (info.attribute("lang") == "ja_JP@kana")
                    {
                        pginfo->title_pronounce = getFirstText(info);
                    }
                }
                else if (pginfo->title == "")
                {
                    pginfo->title = getFirstText(info);
                }
            }
            else if (info.tagName() == "sub-title" && pginfo->subtitle == "")
            {
                pginfo->subtitle = getFirstText(info);
            }
            else if (info.tagName() == "content")
            {
                pginfo->content = getFirstText(info);
            }
            else if (info.tagName() == "desc" && pginfo->desc == "")
            {
                pginfo->desc = getFirstText(info);
            }
            else if (info.tagName() == "category")
            {
                QString cat = getFirstText(info);
                
                if (cat == "movie" || cat == "series" || 
                    cat == "sports" || cat == "tvshow")
                {
                    if (pginfo->catType.isEmpty())
                        pginfo->catType = cat;
                }
                else if (pginfo->category.isEmpty())
                {
                    pginfo->category = cat;
                }

                if ((cat == "Film" || cat == "film") && !isNorthAmerica)
                {
                    // Hack for tv_grab_uk_rt
                    pginfo->catType = "movie";
                }
            }
            else if (info.tagName() == "date" && pginfo->airdate == "")
            {
                // Movie production year
                QString date = getFirstText(info);
                pginfo->airdate = date.left(4);
            }
            else if (info.tagName() == "star-rating")
            {
                QDomNodeList values = info.elementsByTagName("value");
                QDomElement item;
                QString stars, num, den;
                float avg = 0.0;
                // not sure why the XML suggests multiple ratings,
                // but the following will average them anyway.
                for (unsigned int i = 0; i < values.length(); i++)
                {
                    item = values.item(i).toElement();
                    if (item.isNull())
                        continue;
                    stars = getFirstText(item);
                    num = stars.section('/', 0, 0);
                    den = stars.section('/', 1, 1);
                    if (0.0 >= den.toFloat())
                        continue;
                    avg *= i/(i+1);
                    avg += (num.toFloat()/den.toFloat()) / (i+1);
                }
                pginfo->stars.setNum(avg);
            }
            else if (info.tagName() == "rating")
            {
                // again, the structure of ratings seems poorly represented
                // in the XML.  no idea what we'd do with multiple values.
                QDomNodeList values = info.elementsByTagName("value");
                QDomElement item = values.item(0).toElement();
                if (item.isNull())
                    continue;
                ProgRating rating;
                rating.system = info.attribute("system", "");
                rating.rating = getFirstText(item);
                if ("" != rating.system)
                    pginfo->ratings.append(rating);
            }
            else if (info.tagName() == "previously-shown")
            {
                pginfo->previouslyshown = true;

                QString prevdate = getFirstText(info);
                pginfo->originalairdate = prevdate;
            } 
            else if (info.tagName() == "credits")
            {
                parseCredits(info, pginfo);
            }
            else if (info.tagName() == "episode-num" &&
                     info.attribute("system") == "xmltv_ns")
            {
                int tmp;
                QString episodenum(getFirstText(info));
                episode = episodenum.section('.',1,1);
                episode = episode.section('/',0,0).stripWhiteSpace();
                season = episodenum.section('.',0,0).stripWhiteSpace();
                QString part(episodenum.section('.',2,2));
                QString partnumber(part.section('/',0,0).stripWhiteSpace());
                QString parttotal(part.section('/',1,1).stripWhiteSpace());

                pginfo->catType = "series";

                if (!episode.isEmpty())
                {
                    tmp = episode.toInt() + 1;
                    episode = QString::number(tmp);
                    pginfo->syndicatedepisodenumber = QString("E" + episode);
                }

                if (!season.isEmpty())
                {
                    tmp = season.toInt() + 1;
                    season = QString::number(tmp);
                    pginfo->syndicatedepisodenumber.append(QString("S" + season));
                }

                if (!partnumber.isEmpty())
                {                
                    tmp = partnumber.toInt() + 1;
                    partnumber = QString::number(tmp);
                }
                
                if (partnumber != 0 && parttotal >= partnumber && !parttotal.isEmpty())
                {
                    pginfo->parttotal = parttotal;
                    pginfo->partnumber = partnumber;
                }
            }
            else if (info.tagName() == "episode-num" &&
                     info.attribute("system") == "onscreen" &&
                     pginfo->subtitle.isEmpty())
            {
                 pginfo->catType = "series";
                 pginfo->subtitle = getFirstText(info);
            }
        }
    }

    if (pginfo->category.isEmpty() && !pginfo->catType.isEmpty())
        pginfo->category = pginfo->catType;

    /* Do what MythWeb does and assume that programmes with
       star-rating in America are movies. This allows us to
       unify app code with grabbers which explicitly deliver that
       info. */
    if (isNorthAmerica && pginfo->catType == "" &&
        pginfo->stars != "" && pginfo->airdate != "")
        pginfo->catType = "movie";
    
    /* Hack for teveblad grabber to do something with the content tag*/
    if (pginfo->content != "")
    {
        if (pginfo->category == "film")
        {
            pginfo->subtitle = pginfo->desc;
            pginfo->desc = pginfo->content;
        }
        else if (pginfo->desc != "") 
        {
            pginfo->desc = pginfo->desc + " - " + pginfo->content;
        }
        else if (pginfo->desc == "")
        {
            pginfo->desc = pginfo->content;
        }
    }
    
    if (pginfo->airdate.isEmpty())
        pginfo->airdate = QDate::currentDate().toString("yyyy");

    /* Let's build ourself a programid */
    QString programid;
    
    if (pginfo->catType == "movie")
        programid = "MV";
    else if (pginfo->catType == "series")
        programid = "EP";
    else if (pginfo->catType == "sports")
        programid = "SP";
    else
        programid = "SH";
    
    if (!uniqueid.isEmpty()) // we already have a unique id ready for use
        programid.append(uniqueid);
    else
    {
        if (seriesid.isEmpty()) //need to hash ourself a seriesid from the title
        {
            seriesid = QString::number(ELFHash(pginfo->title));
        }
        pginfo->seriesid = seriesid;
        programid.append(seriesid);

        if (!episode.isEmpty() && !season.isEmpty())
        {
            programid.append(episode);
            programid.append(season);
            if (!pginfo->partnumber.isEmpty() && !pginfo->parttotal.isEmpty())
            {
                programid.append(pginfo->partnumber);
                programid.append(pginfo->parttotal);
            }
        }
        else
        {
            /* No ep/season info? Well then remove the programid and rely on
               normal dupchecking methods instead. */
            if (pginfo->catType != "movie")
                programid = "";
        }
    }
    
    pginfo->programid = programid;

    return pginfo;
}
                  
bool parseFile(QString filename, QValueList<ChanInfo> *chanlist,
               QMap<QString, QValueList<ProgInfo> > *proglist)
{
    QDomDocument doc;
    QFile f;

    if (!dash_open(f, filename, IO_ReadOnly))
    {
        cerr << QString("Error unable to open '%1' for reading.")
                .arg(filename)
             << endl;
        return false;
    }

    QString errorMsg = "unknown";
    int errorLine = 0;
    int errorColumn = 0;

    if (!doc.setContent(&f, &errorMsg, &errorLine, &errorColumn))
    {
        cerr << "Error in " << errorLine << ":" << errorColumn << ": "
             << errorMsg << endl;

        f.close();
        return true;
    }

    f.close();

    // now we calculate the localTimezoneOffset, so that we can fix
    // the programdata if needed
    QString config_offset = gContext->GetSetting("TimeOffset", "None");
    // we disable this feature by setting it invalid (> 840min = 14hr)
    int localTimezoneOffset = 841;

    if (config_offset == "Auto")
    {
        localTimezoneOffset = -841; // we mark auto with the -ve of the disable magic number
    }
    else if (config_offset != "None")
    {
        localTimezoneOffset = TimezoneToInt(config_offset);
        if (abs(localTimezoneOffset) > 840)
        {
            cerr << "Ignoring invalid TimeOffset " << config_offset << endl;
            localTimezoneOffset = 841;
        }
    }

    QDomElement docElem = doc.documentElement();

    QUrl baseUrl(docElem.attribute("source-data-url", ""));

    QUrl sourceUrl(docElem.attribute("source-info-url", ""));
    if (sourceUrl.toString() == "http://labs.zap2it.com/")
    {
        cerr << "Don't use tv_grab_na_dd, use the internal datadirect grabber."
             << endl;
        exit(FILLDB_BUGGY_EXIT_SRC_IS_DD);
    }

    QString aggregatedTitle;
    QString aggregatedDesc;
    QString groupingTitle;
    QString groupingDesc;

    QDomNode n = docElem.firstChild();
    while (!n.isNull())
    {
        QDomElement e = n.toElement();
        if (!e.isNull()) 
        {
            if (e.tagName() == "channel")
            {
                ChanInfo *chinfo = parseChannel(e, baseUrl);
                chanlist->push_back(*chinfo);
                delete chinfo;
            }
            else if (e.tagName() == "programme")
            {
                ProgInfo *pginfo = parseProgram(e, localTimezoneOffset);

                if (pginfo->startts == pginfo->endts)
                {
                    /* Not a real program : just a grouping marker */
                    if (!pginfo->title.isEmpty())
                        groupingTitle = pginfo->title + " : ";

                    if (!pginfo->desc.isEmpty())
                        groupingDesc = pginfo->desc + " : ";
                }
                else
                {
                    if (pginfo->clumpidx.isEmpty())
                    {
                        if (!groupingTitle.isEmpty())
                        {
                            pginfo->title.prepend(groupingTitle);
                            groupingTitle = "";
                        }

                        if (!groupingDesc.isEmpty())
                        {
                            pginfo->desc.prepend(groupingDesc);
                            groupingDesc = "";
                        }

                        (*proglist)[pginfo->channel].push_back(*pginfo);
                    }
                    else
                    {
                        /* append all titles/descriptions from one clump */
                        if (pginfo->clumpidx.toInt() == 0)
                        {
                            aggregatedTitle = "";
                            aggregatedDesc = "";
                        }

                        if (!pginfo->title.isEmpty())
                        {
                            if (!aggregatedTitle.isEmpty())
                                aggregatedTitle.append(" | ");
                            aggregatedTitle.append(pginfo->title);
                        }

                        if (!pginfo->desc.isEmpty())
                        {
                            if (!aggregatedDesc.isEmpty())
                                aggregatedDesc.append(" | ");
                            aggregatedDesc.append(pginfo->desc);
                        }    
                        if (pginfo->clumpidx.toInt() == 
                            pginfo->clumpmax.toInt() - 1)
                        {
                            pginfo->title = aggregatedTitle;
                            pginfo->desc = aggregatedDesc;
                            (*proglist)[pginfo->channel].push_back(*pginfo);
                        }
                    }
                }
                delete pginfo;
            }
        }
        n = n.nextSibling();
    }

    return true;
}

bool conflict(ProgInfo &a, ProgInfo &b)
{
    if ((a.start <= b.start && b.start < a.end) ||
        (b.end <= a.end && a.start < b.end))
        return true;
    return false;
}

void fixProgramList(QValueList<ProgInfo> *fixlist)
{
    qHeapSort(*fixlist);

    QValueList<ProgInfo>::iterator i = fixlist->begin();
    QValueList<ProgInfo>::iterator cur;
    while (1)    
    {
        cur = i;
        i++;
        // fill in miss stop times
        if ((*cur).endts == "" || (*cur).startts > (*cur).endts)
        {
            if (i != fixlist->end())
            {
                (*cur).endts = (*i).startts;
                (*cur).end = (*i).start;
            }
            else
            {
                (*cur).end = (*cur).start;
                if ((*cur).end < QDateTime((*cur).end.date(), QTime(6, 0)))
                {
                    (*cur).end.setTime(QTime(6, 0));
                }
                else
                {
                   (*cur).end.setTime(QTime(0, 0));
                   (*cur).end.setDate((*cur).end.date().addDays(1));
                }

                (*cur).endts = (*cur).end.toString("yyyyMMddhhmmss").ascii();
            }
        }
        if (i == fixlist->end())
            break;
        // remove overlapping programs
        if (conflict(*cur, *i))
        {
            QValueList<ProgInfo>::iterator tokeep, todelete;

            if ((*cur).end <= (*cur).start)
                tokeep = i, todelete = cur;
            else if ((*i).end <= (*i).start)
                tokeep = cur, todelete = i;
            else if ((*cur).subtitle != "" && (*i).subtitle == "")
                tokeep = cur, todelete = i;
            else if ((*i).subtitle != "" && (*cur).subtitle == "")
                tokeep = i, todelete = cur;
            else if ((*cur).desc != "" && (*i).desc == "")
                tokeep = cur, todelete = i;
            else if ((*i).desc != "" && (*cur).desc == "")
                tokeep = i, todelete = cur;
            else
                tokeep = i, todelete = cur;

            if (!quiet)
            {
                cerr << "removing conflicting program: "
                     << (*todelete).channel << " "
                     << (*todelete).title.local8Bit() << " "
                     << (*todelete).startts << "-" << (*todelete).endts << endl;
                cerr << "conflicted with             : "
                     << (*tokeep).channel << " "
                     << (*tokeep).title.local8Bit() << " "
                     << (*tokeep).startts << "-" <<   (*tokeep).endts << endl;
                cerr << endl;
            }

            if (todelete == i)
                i = cur;
            fixlist->erase(todelete);
        }
    }
}

QString getResponse(const QString &query, const QString &def)
{
    cout << query;

    if (def != "")
    {
        cout << " [" << (const char *)def.local8Bit() << "]  ";
    }
    
    char response[80];
    cin.getline(response, 80);

    QString qresponse = QString::fromLocal8Bit(response);

    if (qresponse == "")
        qresponse = def;

    return qresponse;
}

unsigned int promptForChannelUpdates(QValueList<ChanInfo>::iterator chaninfo, 
                                     unsigned int chanid)
{
    if (chanid == 0)
    {
        // Default is 0 to allow rapid skipping of many channels,
        // in some xmltv outputs there may be over 100 channel, but
        // only 10 or so that are available in each area.
        chanid = atoi(getResponse("Choose a channel ID (positive integer) ",
                                  "0"));

        // If we wish to skip this channel, use the default 0 and return.
        if (chanid == 0)
            return(0);
    }

    (*chaninfo).name = getResponse("Choose a channel name (any string, "
                                   "long version) ",(*chaninfo).name);
    (*chaninfo).callsign = getResponse("Choose a channel callsign (any string, "
                                       "short version) ",(*chaninfo).callsign);

    if (channel_preset)
    {
        (*chaninfo).chanstr = getResponse("Choose a channel preset (0..999) ",
                                         (*chaninfo).chanstr);
        (*chaninfo).freqid  = getResponse("Choose a frequency id (just like "
                                          "xawtv) ",(*chaninfo).freqid);
    }
    else
    {
        (*chaninfo).chanstr  = getResponse("Choose a channel number (just like "
                                           "xawtv) ",(*chaninfo).chanstr);
        (*chaninfo).freqid = (*chaninfo).chanstr;
    }

    (*chaninfo).finetune = getResponse("Choose a channel fine tune offset (just"
                                       " like xawtv) ",(*chaninfo).finetune);

    (*chaninfo).tvformat = getResponse("Choose a TV format "
                                       "(PAL/SECAM/NTSC/ATSC/Default) ",
                                       (*chaninfo).tvformat);

    (*chaninfo).iconpath = getResponse("Choose a channel icon image (any path "
                                       "name) ",(*chaninfo).iconpath);

    return(chanid);
}

static QString SetupIconCacheDirectory()
{
    QString fileprefix = MythContext::GetConfDir();

    QDir dir(fileprefix);
    if (!dir.exists())
        dir.mkdir(fileprefix);

    fileprefix += "/channels";

    dir = QDir(fileprefix);
    if (!dir.exists())
        dir.mkdir(fileprefix);

    return fileprefix;
}

void handleChannels(int id, QValueList<ChanInfo> *chanlist)
{
    QString fileprefix = SetupIconCacheDirectory();

    QDir::setCurrent(fileprefix);

    fileprefix += "/";

    QValueList<ChanInfo>::iterator i = chanlist->begin();
    for (; i != chanlist->end(); i++)
    {
        QString localfile = "";

        if ((*i).iconpath != "")
        {
            QDir remotefile = QDir((*i).iconpath);
            QString filename = remotefile.dirName();

            localfile = fileprefix + filename;
            QFile actualfile(localfile);
            if (!actualfile.exists())
            {
                QString command = QString("wget ") + (*i).iconpath;
                system(command);
            }
        }

        MSqlQuery query(MSqlQuery::InitCon());

        QString querystr;

        if ((*i).old_xmltvid != "")
        {
            querystr.sprintf("SELECT xmltvid FROM channel WHERE xmltvid = '%s'",
                             (*i).old_xmltvid.ascii());
            query.exec(querystr);

            if (query.isActive() && query.size() > 0)
            {
                if (!quiet)
                    cout << "Converting old xmltvid (" << (*i).old_xmltvid << ") to new ("
                         << (*i).xmltvid << ")\n";

                query.exec(QString("UPDATE channel SET xmltvid = '%1' WHERE xmltvid = '%2';")
                            .arg((*i).xmltvid)
                            .arg((*i).old_xmltvid));

                if (!query.numRowsAffected())
                    MythContext::DBError("xmltvid conversion",query);
            }
        }

        querystr.sprintf("SELECT chanid,name,callsign,channum,finetune,"
                         "icon,freqid,tvformat FROM channel WHERE "
                         "xmltvid = '%s' AND sourceid = %d;", 
                         (*i).xmltvid.ascii(), id); 

        query.exec(querystr);
        if (query.isActive() && query.size() > 0)
        {
            query.next();

            QString chanid = query.value(0).toString();
            if (interactive)
            {
                QString name     = QString::fromUtf8(query.value(1).toString());
                QString callsign = QString::fromUtf8(query.value(2).toString());
                QString chanstr  = QString::fromUtf8(query.value(3).toString());
                QString finetune = QString::fromUtf8(query.value(4).toString());
                QString icon     = QString::fromUtf8(query.value(5).toString());
                QString freqid   = QString::fromUtf8(query.value(6).toString());
                QString tvformat = QString::fromUtf8(query.value(7).toString());

                cout << "### " << endl;
                cout << "### Existing channel found" << endl;
                cout << "### " << endl;
                cout << "### xmltvid  = " << (*i).xmltvid.local8Bit() << endl;
                cout << "### chanid   = " << chanid.local8Bit()       << endl;
                cout << "### name     = " << name.local8Bit()         << endl;
                cout << "### callsign = " << callsign.local8Bit()     << endl;
                cout << "### channum  = " << chanstr.local8Bit()      << endl;
                if (channel_preset)
                    cout << "### freqid   = " << freqid.local8Bit()   << endl;
                cout << "### finetune = " << finetune.local8Bit()     << endl;
                cout << "### tvformat = " << tvformat.local8Bit()     << endl;
                cout << "### icon     = " << icon.local8Bit()         << endl;
                cout << "### " << endl;

                (*i).name = name;
                (*i).callsign = callsign;
                (*i).chanstr  = chanstr;
                (*i).finetune = finetune;
                (*i).freqid = freqid;
                (*i).tvformat = tvformat;

                promptForChannelUpdates(i, atoi(chanid.ascii()));

                if ((*i).callsign == "")
                    (*i).callsign = chanid;

                if (name     != (*i).name ||
                    callsign != (*i).callsign ||
                    chanstr  != (*i).chanstr ||
                    finetune != (*i).finetune ||
                    freqid   != (*i).freqid ||
                    icon     != localfile ||
                    tvformat != (*i).tvformat)
                {
                    MSqlQuery subquery(MSqlQuery::InitCon());
                    subquery.prepare("UPDATE channel SET chanid = :CHANID, "
                                     "name = :NAME, callsign = :CALLSIGN, "
                                     "channum = :CHANNUM, finetune = :FINE, "
                                     "icon = :ICON, freqid = :FREQID, "
                                     "tvformat = :TVFORMAT "
                                     " WHERE xmltvid = :XMLTVID "
                                     "AND sourceid = :SOURCEID;");
                    subquery.bindValue(":CHANID", chanid);
                    subquery.bindValue(":NAME", (*i).name.utf8());
                    subquery.bindValue(":CALLSIGN", (*i).callsign.utf8());
                    subquery.bindValue(":CHANNUM", (*i).chanstr);
                    subquery.bindValue(":FINE", (*i).finetune.toInt());
                    subquery.bindValue(":ICON", localfile);
                    subquery.bindValue(":FREQID", (*i).freqid);
                    subquery.bindValue(":TVFORMAT", (*i).tvformat);
                    subquery.bindValue(":XMLTVID", (*i).xmltvid);
                    subquery.bindValue(":SOURCEID", id);

                    if (!subquery.exec())
                    {
                        cerr << "DB Error: Channel update failed, SQL query "
                             << "was:" << endl;
                        cerr << querystr << endl;
                    }
                    else
                    {
                        cout << "### " << endl;
                        cout << "### Change performed" << endl;
                        cout << "### " << endl;
                    }
                }
                else
                {
                    cout << "### " << endl;
                    cout << "### Nothing changed" << endl;
                    cout << "### " << endl;
                }
            }
            else
            {
                if (!non_us_updating && localfile != "")
                {
                    MSqlQuery subquery(MSqlQuery::InitCon());
                    subquery.prepare("UPDATE channel SET icon = :ICON WHERE "
                                     "chanid = :CHANID;");
                    subquery.bindValue(":ICON", localfile);
                    subquery.bindValue(":CHANID", chanid);

                    if (!subquery.exec())
                        MythContext::DBError("Channel icon change", subquery);
                }
            }
        }
        else
        {
            int major, minor, freqid = (*i).freqid.toInt();
            long long freq;
            get_atsc_stuff((*i).chanstr, id, freqid, major, minor, freq);

            if (interactive && ((minor == 0) || (freq > 0)))
            {
                cout << "### " << endl;
                cout << "### New channel found" << endl;
                cout << "### " << endl;
                cout << "### name     = " << (*i).name.local8Bit()     << endl;
                cout << "### callsign = " << (*i).callsign.local8Bit() << endl;
                cout << "### channum  = " << (*i).chanstr.local8Bit()  << endl;
                if (channel_preset)
                    cout << "### freqid   = " << freqid                << endl;
                cout << "### finetune = " << (*i).finetune.local8Bit() << endl;
                cout << "### tvformat = " << (*i).tvformat.local8Bit() << endl;
                cout << "### icon     = " << localfile.local8Bit()     << endl;
                cout << "### " << endl;

                uint chanid = promptForChannelUpdates(i,0);

                if ((*i).callsign == "")
                    (*i).callsign = QString::number(chanid);

                int mplexid = 0;
                if ((chanid > 0) && (minor > 0))
                    mplexid = ChannelUtil::CreateMultiplex(id,   "atsc",
                                                           freq, "8vsb");

                if (((mplexid > 0) || ((minor == 0) && (chanid > 0))) &&
                    ChannelUtil::CreateChannel(
                        mplexid,          id,               chanid,
                        (*i).callsign,    (*i).name,        (*i).chanstr,
                        0 /*service id*/, major,            minor,
                        false /*use on air guide*/, false /*hidden*/,
                        false /*hidden in guide*/,
                        freqid,           localfile,        (*i).tvformat,
                        (*i).xmltvid))
                {
                    cout << "### " << endl;
                    cout << "### Channel inserted" << endl;
                    cout << "### " << endl;
                }
                else
                {
                    cout << "### " << endl;
                    cout << "### Channel skipped" << endl;
                    cout << "### " << endl;
                }
            }
            else if (!non_us_updating && ((minor == 0) || (freq > 0)))
            {
                // We only do this if we are not asked to skip it with the
                // --updating flag.
                int mplexid = 0, chanid = 0;
                if (minor > 0)
                {
                    mplexid = ChannelUtil::CreateMultiplex(
                        id, "atsc", freq, "8vsb");
                }

                if ((mplexid > 0) || (minor == 0))
                    chanid = ChannelUtil::CreateChanID(id, (*i).chanstr);

                if ((*i).callsign.isEmpty())
                {
                    QStringList words = QStringList::split(" ",
                                        (*i).name.simplifyWhiteSpace().upper());
                    QString callsign = "";
                    if (words[0].isEmpty())
                        callsign = QString::number(chanid);
                    else if (words[1].isEmpty())
                        callsign = words[0].left(5);
                    else
                    {
                        callsign = words[0].left(words[1].length() == 1 ? 4:3);
                        callsign += words[1].left(5 - callsign.length());
                    }
                    (*i).callsign = callsign;
                }

                if (chanid > 0)
                {
                    QString cstr = QString((*i).chanstr);
                    if(channel_preset && cstr.isEmpty())
                        cstr = QString::number(chanid % 1000);

                    ChannelUtil::CreateChannel(
                        mplexid,          id,        chanid,
                        (*i).callsign,    (*i).name, cstr,
                        0 /*service id*/, major,     minor,
                        false /*use on air guide*/,  false /*hidden*/,
                        false /*hidden in guide*/,
                        freqid,      localfile, (*i).tvformat,
                        (*i).xmltvid);
                }
            }
        }
    }

    UpdateSourceIcons(id);
}

void clearDBAtOffset(int offset, int chanid, QDate *qCurrentDate)
{
    if (no_delete)
        return;

    QDate newDate; 
    if (qCurrentDate == 0)
    {
        newDate = QDate::currentDate();
        qCurrentDate = &newDate;
    }

    int nextoffset = 1;

    if (offset == -1)
    {
        offset = 0;
        nextoffset = 10;
    }

    QDateTime from, to;
    from.setDate(*qCurrentDate);
    from = from.addDays(offset);
    from = from.addSecs(listing_wrap_offset);
    to = from.addDays(nextoffset);

    clearDataByChannel(chanid, from, to);
}

void handlePrograms(int id, QMap<QString, QValueList<ProgInfo> > *proglist)
{
    int unchanged = 0, updated = 0;
    QMap<QString, QValueList<ProgInfo> >::Iterator mapiter;

    for (mapiter = proglist->begin(); mapiter != proglist->end(); ++mapiter)
    {
        MSqlQuery query(MSqlQuery::InitCon()), chanQuery(MSqlQuery::InitCon());

        if (mapiter.key() == "")
            continue;

        int chanid = 0;

        chanQuery.prepare("SELECT chanid FROM channel WHERE sourceid = :ID AND "
                          "xmltvid = :XMLTVID;"); 
        chanQuery.bindValue(":ID", id);
        chanQuery.bindValue(":XMLTVID", mapiter.key());

        chanQuery.exec();

        if (!chanQuery.isActive() || chanQuery.size() <= 0)
        {
            cerr << "Unknown xmltv channel identifier: " << mapiter.key()
                 << endl << "Skipping channel.\n";
            continue;
        }

        while (chanQuery.next())
        {
            chanid = chanQuery.value(0).toInt();

            if (chanid == 0)
            {
                cerr << "Unknown xmltv channel identifier: " << mapiter.key()
                     << endl << "Skipping channel.\n";
                continue;
            }

            QValueList<ProgInfo> *sortlist = &((*proglist)[mapiter.key()]);

            fixProgramList(sortlist);

            QValueList<ProgInfo>::iterator i = sortlist->begin();
            for (; i != sortlist->end(); i++)
            {
                query.prepare("SELECT * FROM program WHERE "
                              "chanid=:CHANID AND starttime=:START AND "
                              "endtime=:END AND title=:TITLE AND "
                              "subtitle=:SUBTITLE AND description=:DESC AND "
                              "category=:CATEGORY AND "
                              "category_type=:CATEGORY_TYPE AND "
                              "airdate=:AIRDATE AND stars=:STARS AND "
                              "previouslyshown=:PREVIOUSLYSHOWN AND "
                              "title_pronounce=:TITLE_PRONOUNCE AND "
                              "stereo=:STEREO AND subtitled=:SUBTITLED AND "
                              "hdtv=:HDTV AND "
                              "closecaptioned=:CLOSECAPTIONED AND "
                              "partnumber=:PARTNUMBER AND "
                              "parttotal=:PARTTOTAL AND "
                              "seriesid=:SERIESID AND "
                              "showtype=:SHOWTYPE AND "
                              "colorcode=:COLORCODE AND "
                              "syndicatedepisodenumber=:SYNDICATEDEPISODENUMBER AND "
                              "programid=:PROGRAMID;");
                query.bindValue(":CHANID", chanid);
                query.bindValue(":START", (*i).start);
                query.bindValue(":END", (*i).end);
                query.bindValue(":TITLE", (*i).title.utf8());
                query.bindValue(":SUBTITLE", (*i).subtitle.utf8());
                query.bindValue(":DESC", (*i).desc.utf8());
                query.bindValue(":CATEGORY", (*i).category.utf8());
                query.bindValue(":CATEGORY_TYPE", (*i).catType.utf8());
                query.bindValue(":AIRDATE", (*i).airdate.utf8());
                query.bindValue(":STARS", (*i).stars.utf8());
                query.bindValue(":PREVIOUSLYSHOWN", (*i).previouslyshown);
                query.bindValue(":TITLE_PRONOUNCE", (*i).title_pronounce.utf8());
                query.bindValue(":STEREO", (*i).stereo);
                query.bindValue(":SUBTITLED", (*i).subtitled);
                query.bindValue(":HDTV", (*i).hdtv);
                query.bindValue(":CLOSECAPTIONED", (*i).closecaptioned);
                query.bindValue(":PARTNUMBER", (*i).partnumber);
                query.bindValue(":PARTTOTAL", (*i).parttotal);
                query.bindValue(":SERIESID", (*i).seriesid);
                query.bindValue(":SHOWTYPE", (*i).showtype);
                query.bindValue(":COLORCODE", (*i).colorcode);
                query.bindValue(":SYNDICATEDEPISODENUMBER", (*i).syndicatedepisodenumber);
                query.bindValue(":PROGRAMID", (*i).programid);
                query.exec();

                if (query.isActive() && query.size() > 0)
                {
                    unchanged++;
                    continue;
                }

                query.prepare("SELECT title,starttime,endtime FROM program "
                              "WHERE chanid=:CHANID AND starttime>=:START AND "
                              "starttime<:END;");
                query.bindValue(":CHANID", chanid);
                query.bindValue(":START", (*i).start);
                query.bindValue(":END", (*i).end);
                query.exec();

                if (query.isActive() && query.size() > 0)
                {
                    if (!quiet)
                    {
                        while (query.next())
                        {
                            cerr << "removing existing program: "
                                 << (*i).channel.local8Bit() << " "
                                 << QString::fromUtf8(query.value(0).toString()).local8Bit() << " "
                                 << query.value(1).toDateTime().toString(Qt::ISODate) << " - "
                                 << query.value(2).toDateTime().toString(Qt::ISODate) << endl;
                        }

                        cerr << "inserting new program    : "
                             << (*i).channel.local8Bit() << " "
                             << (*i).title.local8Bit() << " "
                             << (*i).start.toString() << " - " 
                             << (*i).end.toString() << endl << endl;
                    }

                    MSqlQuery subquery(MSqlQuery::InitCon());
                    subquery.prepare("DELETE FROM program WHERE "
                                     "chanid=:CHANID AND starttime>=:START "
                                     "AND starttime<:END;");
                    subquery.bindValue(":CHANID", chanid);
                    subquery.bindValue(":START", (*i).start);
                    subquery.bindValue(":END", (*i).end);

                    subquery.exec();

                    subquery.prepare("DELETE FROM programrating WHERE "
                                     "chanid=:CHANID AND starttime>=:START "
                                     "AND starttime<:END;");
                    subquery.bindValue(":CHANID", chanid);
                    subquery.bindValue(":START", (*i).start);
                    subquery.bindValue(":END", (*i).end);
 
                    subquery.exec();

                    subquery.prepare("DELETE FROM credits WHERE "
                                     "chanid=:CHANID AND starttime>=:START "
                                     "AND starttime<:END;");
                    subquery.bindValue(":CHANID", chanid);
                    subquery.bindValue(":START", (*i).start);
                    subquery.bindValue(":END", (*i).end);

                    subquery.exec();
                }

                query.prepare("INSERT INTO program (chanid,starttime,endtime,"
                              "title,subtitle,description,category,"
                              "category_type,airdate,stars,previouslyshown,"
                              "title_pronounce,stereo,subtitled,hdtv,"
                              "closecaptioned,partnumber,parttotal,"
                              "seriesid,originalairdate,showtype,colorcode,"
                              "syndicatedepisodenumber,programid) "
                              "VALUES(:CHANID,:STARTTIME,:ENDTIME,:TITLE,"
                              ":SUBTITLE,:DESCRIPTION,:CATEGORY,:CATEGORY_TYPE,"
                              ":AIRDATE,:STARS,:PREVIOUSLYSHOWN,"
                              ":TITLE_PRONOUNCE,:STEREO,:SUBTITLED,:HDTV,"
                              ":CLOSECAPTIONED,:PARTNUMBER,:PARTTOTAL,"
                              ":SERIESID,:ORIGINALAIRDATE,:SHOWTYPE,:COLORCODE,"
                              ":SYNDICATEDEPISODENUMBER,:PROGRAMID);");
                query.bindValue(":CHANID", chanid);
                query.bindValue(":STARTTIME", (*i).start);
                query.bindValue(":ENDTIME", (*i).end);
                query.bindValue(":TITLE", (*i).title.utf8());
                query.bindValue(":SUBTITLE", (*i).subtitle.utf8());
                query.bindValue(":DESCRIPTION", (*i).desc.utf8());
                query.bindValue(":CATEGORY", (*i).category.utf8());
                query.bindValue(":CATEGORY_TYPE", (*i).catType.utf8());
                query.bindValue(":AIRDATE", (*i).airdate.utf8());
                query.bindValue(":STARS", (*i).stars.utf8());
                query.bindValue(":PREVIOUSLYSHOWN", (*i).previouslyshown);
                query.bindValue(":TITLE_PRONOUNCE", (*i).title_pronounce.utf8());
                query.bindValue(":STEREO", (*i).stereo);
                query.bindValue(":SUBTITLED", (*i).subtitled);
                query.bindValue(":HDTV", (*i).hdtv);
                query.bindValue(":CLOSECAPTIONED", (*i).closecaptioned);
                query.bindValue(":PARTNUMBER", (*i).partnumber);
                query.bindValue(":PARTTOTAL", (*i).parttotal);
                query.bindValue(":SERIESID", (*i).seriesid);
                query.bindValue(":ORIGINALAIRDATE", (*i).originalairdate);
                query.bindValue(":SHOWTYPE", (*i).showtype);
                query.bindValue(":COLORCODE", (*i).colorcode);
                query.bindValue(":SYNDICATEDEPISODENUMBER", (*i).syndicatedepisodenumber);
                query.bindValue(":PROGRAMID", (*i).programid);
                if (!query.exec())
                    MythContext::DBError("program insert", query);

                updated++;

                QValueList<ProgRating>::iterator j = (*i).ratings.begin();
                for (; j != (*i).ratings.end(); j++)
                {
                    query.prepare("INSERT INTO programrating (chanid,starttime,"
                                  "system,rating) VALUES (:CHANID,:START,:SYS,"
                                  ":RATING);");
                    query.bindValue(":CHANID", chanid);
                    query.bindValue(":START", (*i).start);
                    query.bindValue(":SYS", (*j).system.utf8());
                    query.bindValue(":RATING", (*j).rating.utf8());

                    if (!query.exec())
                        MythContext::DBError("programrating insert", query);
                }

                QValueList<ProgCredit>::iterator k = (*i).credits.begin();
                for (; k != (*i).credits.end(); k++)
                {
                    query.prepare("SELECT person FROM people WHERE "
                                  "name = :NAME;");
                    query.bindValue(":NAME", (*k).name.utf8());
                    if (!query.exec())
                        MythContext::DBError("person lookup", query);

                    int personid = -1;
                    if (query.isActive() && query.size() > 0)
                    {
                        query.next();
                        personid = query.value(0).toInt();
                    }

                    if (personid < 0)
                    {
                        query.prepare("INSERT INTO people (name) VALUES "
                                      "(:NAME);");
                        query.bindValue(":NAME", (*k).name.utf8());
                        if (!query.exec())
                            MythContext::DBError("person insert", query);

                        query.prepare("SELECT person FROM people WHERE "
                                      "name = :NAME;");
                        query.bindValue(":NAME", (*k).name.utf8());
                        if (!query.exec())
                            MythContext::DBError("person lookup", query);

                        if (query.isActive() && query.size() > 0)
                        {
                            query.next();
                            personid = query.value(0).toInt();
                        }
                    }

                    if (personid < 0)
                    {
                        cerr << "Error inserting person\n";
                        continue;
                    }

                    query.prepare("INSERT INTO credits (chanid,starttime,"
                                  "role,person) VALUES "
                                  "(:CHANID, :START, :ROLE, :PERSON);");
                    query.bindValue(":CHANID", chanid);
                    query.bindValue(":START", (*i).start);
                    query.bindValue(":ROLE", (*k).role.utf8());
                    query.bindValue(":PERSON", personid);
                    if (!query.exec())
                    {
                        // be careful of the startime/timestamp "feature"!
                        query.prepare("UPDATE credits SET "
                                      "role = concat(role,',:ROLE'), "
                                      "starttime = :START "
                                      "WHERE chanid = :CHANID AND "
                                      "starttime = :START2 and person = :PERSON");
                        query.bindValue(":ROLE", (*k).role.utf8());
                        query.bindValue(":START", (*i).start);
                        query.bindValue(":CHANID", chanid);
                        query.bindValue(":START2", (*i).start);
                        query.bindValue(":PERSON", personid);

                        if (!query.exec())
                            MythContext::DBError("credits update", query);
                    }
                }
            }
        }
    }
    if (!quiet)
    {
        cerr << "Updated programs: " << updated
             << "  Unchanged programs: " << unchanged << endl;
    }
}

bool grabDataFromFile(int id, QString &filename)
{
    QValueList<ChanInfo> chanlist;
    QMap<QString, QValueList<ProgInfo> > proglist;

    if (!parseFile(filename, &chanlist, &proglist))
        return false;

    handleChannels(id, &chanlist);
    if (proglist.count() == 0)
    {
        VERBOSE(VB_GENERAL,
                QString("No programs found in data."));
        endofdata = true;
    }
    else
    {
        handlePrograms(id, &proglist);
    }

    return true;
}

time_t toTime_t(QDateTime &dt)
{
    tm brokenDown;
    brokenDown.tm_sec = dt.time().second();
    brokenDown.tm_min = dt.time().minute();
    brokenDown.tm_hour = dt.time().hour();
    brokenDown.tm_mday = dt.date().day();
    brokenDown.tm_mon = dt.date().month() - 1;
    brokenDown.tm_year = dt.date().year() - 1900;
    brokenDown.tm_isdst = -1;
    int secsSince1Jan1970UTC = (int) mktime( &brokenDown );
    if ( secsSince1Jan1970UTC < -1 )
        secsSince1Jan1970UTC = -1;
    return secsSince1Jan1970UTC;
}

bool grabData(Source source, int offset, QDate *qCurrentDate = 0)
{
    QString xmltv_grabber = source.xmltvgrabber;

    if (xmltv_grabber == "datadirect")
        return grabDDData(source, offset, *qCurrentDate, DD_ZAP2IT);
    if (xmltv_grabber == "schedulesdirect1")
        return grabDDData(source, offset, *qCurrentDate, DD_SCHEDULES_DIRECT);

    char tempfilename[] = "/tmp/mythXXXXXX";
    if (mkstemp(tempfilename) == -1)
    {
        VERBOSE(VB_IMPORTANT,
                QString("Error creating temporary file in /tmp, %1")
                .arg(strerror(errno)));
        exit(FILLDB_BUGGY_EXIT_ERR_OPEN_TMPFILE);
    }

    QString filename = QString(tempfilename);

    QString home = QDir::homeDirPath();
    QString configfile = QString("%1/%2.xmltv").arg(MythContext::GetConfDir())
                                                       .arg(source.name);

    QString command  = QString("nice %1 --config-file '%2' --output %3")
                            .arg(xmltv_grabber.ascii())
                            .arg(configfile.ascii())
                            .arg(filename.ascii());

    // The one concession to grabber specific behaviour.
    // Will be removed when the grabber allows.
    if (xmltv_grabber == "tv_grab_jp")
    {
        command += QString(" --enable-readstr");
        isJapan = true;
    }
    else if (source.xmltvgrabber_prefmethod != "allatonce")
    {
        // XMLTV Docs don't recommend grabbing one day at a
        // time but the current myth code is heavily geared
        // that way so until it is re-written behave as
        // we always have done.
        command += QString(" --days 1 --offset %1").arg(offset);
    }

    command += graboptions;

    if (! (print_verbose_messages & VB_GENERAL))
        command += " --quiet";

    QDateTime qdtNow = QDateTime::currentDateTime();
    MSqlQuery query(MSqlQuery::InitCon());
    QString status = "currently running.";

    query.exec(QString("UPDATE settings SET data ='%1' "
                       "WHERE value='mythfilldatabaseLastRunStart'")
                       .arg(qdtNow.toString("yyyy-MM-dd hh:mm")));

    query.exec(QString("UPDATE settings SET data ='%1' "
                       "WHERE value='mythfilldatabaseLastRunStatus'")
                       .arg(status));

    VERBOSE(VB_GENERAL, QString("Grabber Command: %1").arg(command));

    VERBOSE(VB_GENERAL,
            "----------------- Start of XMLTV output -----------------");

    int systemcall_status = system(command.ascii());
    bool succeeded = WIFEXITED(systemcall_status) &&
         WEXITSTATUS(systemcall_status) == 0;

    VERBOSE(VB_GENERAL,
            "------------------ End of XMLTV output ------------------");

    qdtNow = QDateTime::currentDateTime();
    query.exec(QString("UPDATE settings SET data ='%1' "
                       "WHERE value='mythfilldatabaseLastRunEnd'")
                       .arg(qdtNow.toString("yyyy-MM-dd hh:mm")));

    status = "Successful.";

    if (!succeeded)
    {
        status = QString("FAILED:  xmltv returned error code %1.")
                         .arg(systemcall_status);

        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunStatus'")
                           .arg(status));

        VERBOSE(VB_GENERAL, status);

        if (WIFSIGNALED(systemcall_status) &&
            (WTERMSIG(systemcall_status) == SIGINT || WTERMSIG(systemcall_status) == SIGQUIT))
            interrupted = true;
    }

    grabDataFromFile(source.id, filename);

    QFile thefile(filename);
    thefile.remove();

    return succeeded;
}

void grabDataFromDDFile(int id, int offset, const QString &filename,
        const QString &lineupid, QDate *qCurrentDate = 0)
{
    QDate *currentd = qCurrentDate;
    QDate qcd = QDate::currentDate();
    if (!currentd)
        currentd = &qcd;

    ddprocessor.SetInputFile(filename);
    Source s;
    s.id = id;
    s.name = "";
    s.xmltvgrabber = "datadirect";
    s.userid = "fromfile";
    s.password = "fromfile";
    s.lineupid = lineupid;

    grabData(s, offset, currentd);
}

void clearOldDBEntries(void)
{
    MSqlQuery query(MSqlQuery::InitCon());
    QString querystr;
    int offset = 1;

    if (no_delete)
    {
        offset = 7;
        VERBOSE(VB_GENERAL, "Keeping 7 days of data.");
    }

    query.prepare("DELETE FROM oldprogram WHERE airdate < "
                  "DATE_SUB(CURRENT_DATE, INTERVAL 320 DAY);");
    query.exec();

    query.prepare("REPLACE INTO oldprogram (oldtitle,airdate) "
                  "SELECT title,starttime FROM program "
                  "WHERE starttime < NOW() AND manualid = 0 "
                  "GROUP BY title;");
    query.exec();

    query.prepare("DELETE FROM program WHERE starttime <= "
                  "DATE_SUB(CURRENT_DATE, INTERVAL :OFFSET DAY);");
    query.bindValue(":OFFSET", offset);
    query.exec();

    query.prepare("DELETE FROM programrating WHERE starttime <= "
                  "DATE_SUB(CURRENT_DATE, INTERVAL :OFFSET DAY);");
    query.bindValue(":OFFSET", offset);
    query.exec();

    query.prepare("DELETE FROM programgenres WHERE starttime <= "
                  "DATE_SUB(CURRENT_DATE, INTERVAL :OFFSET DAY);");
    query.bindValue(":OFFSET", offset);
    query.exec();

    query.prepare("DELETE FROM credits WHERE starttime <= "
                  "DATE_SUB(CURRENT_DATE, INTERVAL :OFFSET DAY);");
    query.bindValue(":OFFSET", offset);
    query.exec();

    query.prepare("DELETE FROM record WHERE (type = :SINGLE "
                  "OR type = :OVERRIDE OR type = :DONTRECORD) "
                  "AND enddate < CURDATE();");
    query.bindValue(":SINGLE", kSingleRecord);
    query.bindValue(":OVERRIDE", kOverrideRecord);
    query.bindValue(":DONTRECORD", kDontRecord);
    query.exec();

    MSqlQuery findq(MSqlQuery::InitCon());
    findq.prepare("SELECT record.recordid FROM record "
                  "LEFT JOIN oldfind ON oldfind.recordid = record.recordid "
                  "WHERE type = :FINDONE AND oldfind.findid IS NOT NULL;");
    findq.bindValue(":FINDONE", kFindOneRecord);
    findq.exec();
        
    if (findq.isActive() && findq.size() > 0)
    {
        while (findq.next())
        {
            query.prepare("DELETE FROM record WHERE recordid = :RECORDID;");
            query.bindValue(":RECORDID", findq.value(0).toInt());
            query.exec();
        }
    }
    query.prepare("DELETE FROM oldfind WHERE findid < TO_DAYS(NOW()) - 14;");
    query.exec();

    int cleanOldRecorded = gContext->GetNumSetting( "CleanOldRecorded", 10);

    query.prepare("DELETE FROM oldrecorded WHERE "
                  "recstatus <> :RECORDED AND duplicate = 0 AND "
                  "endtime < DATE_SUB(CURRENT_DATE, INTERVAL :CLEAN DAY);");
    query.bindValue(":RECORDED", rsRecorded);
    query.bindValue(":CLEAN", cleanOldRecorded);
    query.exec();
}

/** \fn fillData(QValueList<Source> &sourcelist)
 *  \brief Goes through the sourcelist and updates its channels with
 *         program info grabbed with the associated grabber.
 *  \return true if there was no failures
 */
bool fillData(QValueList<Source> &sourcelist)
{
    QValueList<Source>::Iterator it;
    QValueList<Source>::Iterator it2;

    QString status, querystr;
    MSqlQuery query(MSqlQuery::InitCon());
    QDateTime GuideDataBefore, GuideDataAfter;
    int failures = 0;
    int externally_handled = 0;
    int total_sources = sourcelist.size();
    int source_channels = 0;

    QString sidStr = QString("Updating source #%1 (%2) with grabber %3");

    need_post_grab_proc = false;
    int nonewdata = 0;
    bool has_dd_source = false;

    // find all DataDirect duplicates, so we only data download once.
    for (it = sourcelist.begin(); it != sourcelist.end(); ++it)
    {
        if (!is_grabber_datadirect((*it).xmltvgrabber))
            continue;

        has_dd_source = true;
        for (it2 = sourcelist.begin(); it2 != sourcelist.end(); ++it2)
        {
            if (((*it).id           != (*it2).id)           &&
                ((*it).xmltvgrabber == (*it2).xmltvgrabber) &&
                ((*it).userid       == (*it2).userid)       &&
                ((*it).password     == (*it2).password))
            {
                (*it).dd_dups.push_back((*it2).id);
            }
        }
    }
    if (has_dd_source)
        ddprocessor.CreateTempDirectory();

    for (it = sourcelist.begin(); it != sourcelist.end(); ++it)
    {

        query.prepare("SELECT MAX(endtime) FROM program p LEFT JOIN channel c "
                      "ON p.chanid=c.chanid WHERE c.sourceid= :SRCID "
                      "AND manualid = 0;");
        query.bindValue(":SRCID", (*it).id);
        query.exec();
        if (query.isActive() && query.size() > 0)
        {
            query.next();

            if (!query.isNull(0))
                GuideDataBefore = QDateTime::fromString(query.value(0).toString(),
                                                        Qt::ISODate);
        }

        channel_update_run = false;
        endofdata = false;

        QString xmltv_grabber = (*it).xmltvgrabber;

        if (xmltv_grabber == "eitonly") 
        {
            VERBOSE(VB_GENERAL,
                    QString("Source %1 configured to use only the "
                            "broadcasted guide data. Skipping.")
                    .arg((*it).id));

            externally_handled++;
            query.exec(QString("UPDATE settings SET data ='%1' "
                               "WHERE value='mythfilldatabaseLastRunStart' OR "
                               "value = 'mythfilldatabaseLastRunEnd'")
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));
            continue;
        }
        else if (xmltv_grabber == "/bin/true" ||
                 xmltv_grabber == "none" ||
                 xmltv_grabber == "")
        {
            VERBOSE(VB_GENERAL,
                    QString("Source %1 configured with no grabber. "
                            "Nothing to do.").arg((*it).id));

            externally_handled++;
            query.exec(QString("UPDATE settings SET data ='%1' "
                               "WHERE value='mythfilldatabaseLastRunStart' OR "
                               "value = 'mythfilldatabaseLastRunEnd'")
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));
            continue;
        }

        VERBOSE(VB_GENERAL, sidStr.arg((*it).id)
                                  .arg((*it).name)
                                  .arg(xmltv_grabber));

        query.prepare(
            "SELECT COUNT(chanid) FROM channel WHERE sourceid = "
             ":SRCID AND xmltvid != ''");
        query.bindValue(":SRCID", (*it).id);
        query.exec();

        if (query.isActive() && query.size() > 0)
        {
            query.next();
            source_channels = query.value(0).toInt();
            if (source_channels > 0)
            {
                VERBOSE(VB_GENERAL, QString("Found %1 channels for "
                                "source %2 which use grabber")
                                .arg(source_channels).arg((*it).id));
            }
            else
            {
                VERBOSE(VB_GENERAL, QString("No channels are "
                    "configured to use grabber."));
            }
        }
        else {
            source_channels = 0;
            VERBOSE(VB_GENERAL,
                    QString("Can't get a channel count for source id %1")
                            .arg((*it).id));
        }

        bool hasprefmethod = false;

        if (is_grabber_external(xmltv_grabber))
        {

            QProcess grabber_capabilities_proc(xmltv_grabber);
            grabber_capabilities_proc.addArgument(QString("--capabilities"));
            if ( grabber_capabilities_proc.start() )
            {

                int i=0;
                // Assume it shouldn't take more than 10 seconds
                // Broken versions of QT cause QProcess::start
                // and QProcess::isRunning to return true even
                // when the executable doesn't exist
                while (grabber_capabilities_proc.isRunning() && i < 100)
                {
                    usleep(100000);
                    ++i;
                }

                if (grabber_capabilities_proc.normalExit())
                {
                    QString capabilites = "";

                    while (grabber_capabilities_proc.canReadLineStdout())
                    {
                        QString capability
                            = grabber_capabilities_proc.readLineStdout();
                        capabilites += capability + " ";

                        if (capability == "baseline")
                            (*it).xmltvgrabber_baseline = true;

                        if (capability == "manualconfig")
                            (*it).xmltvgrabber_manualconfig = true;

                        if (capability == "cache")
                            (*it).xmltvgrabber_cache = true;

                        if (capability == "preferredmethod")
                            hasprefmethod = true;
                    }

                    VERBOSE(VB_GENERAL, QString("Grabber has capabilities: %1")
                        .arg(capabilites));
                }
                else {
                    VERBOSE(VB_IMPORTANT, "%1  --capabilities failed or we "
                        "timed out waiting. You may need to upgrade your "
                        "xmltv grabber");
                }
            }
            else {
                QString error = grabber_capabilities_proc.readLineStdout();
                VERBOSE(VB_IMPORTANT, QString("Failed to run %1 "
                        "--capabilities").arg(xmltv_grabber));
            }
        }


        if (hasprefmethod)
        {

            QProcess grabber_method_proc(xmltv_grabber);
            grabber_method_proc.addArgument("--preferredmethod");
            if ( grabber_method_proc.start() )
            {
                int i=0;
                // Assume it shouldn't take more than 10 seconds
                // Broken versions of QT cause QProcess::start
                // and QProcess::isRunning to return true even
                // when the executable doesn't exist
                while (grabber_method_proc.isRunning() && i < 100)
                {
                    usleep(100000);
                    ++i;
                }

                if (grabber_method_proc.normalExit())
                {
                    (*it).xmltvgrabber_prefmethod =
                        grabber_method_proc.readLineStdout();
                }
                else {
                    VERBOSE(VB_IMPORTANT, "%1  --preferredmethod failed or we "
                    "timed out waiting. You may need to upgrade your "
                    "xmltv grabber");
                }

                VERBOSE(VB_GENERAL, QString("Grabber prefers method: %1")
                .arg((*it).xmltvgrabber_prefmethod));
            }
            else {
                QString error = grabber_method_proc.readLineStdout();
                VERBOSE(VB_IMPORTANT, QString("Failed to run %1 --preferredmethod")
                        .arg(xmltv_grabber));
            }
        }

        need_post_grab_proc |= !is_grabber_datadirect(xmltv_grabber);

        if (is_grabber_datadirect(xmltv_grabber) && dd_grab_all)
        {
            if (only_update_channels)
                DataDirectUpdateChannels(*it);
            else
            {
                QDate qCurrentDate = QDate::currentDate();
                grabData(*it, 0, &qCurrentDate);
            }
        }
        else if ((*it).xmltvgrabber_prefmethod == "allatonce")
        {
            if (!grabData(*it, 0))
                ++failures;
        }
        else if ((*it).xmltvgrabber_baseline ||
                 is_grabber_datadirect(xmltv_grabber))
        {

            QDate qCurrentDate = QDate::currentDate();

            // We'll keep grabbing until it returns nothing
            // Max days currently supported is 21
            int grabdays = 21;

            if (maxDays > 0) // passed with --max-days
                grabdays = maxDays;
            else if (is_grabber_datadirect(xmltv_grabber))
                grabdays = 14;

            grabdays = (only_update_channels) ? 1 : grabdays;

            if (grabdays == 1)
                refresh_today = true;

            if (is_grabber_datadirect(xmltv_grabber) && only_update_channels)
            {
                DataDirectUpdateChannels(*it);
                grabdays = 0;
            }

            for (int i = 0; i < grabdays; i++)
            {
                // We need to check and see if the current date has changed 
                // since we started in this loop.  If it has, we need to adjust
                // the value of 'i' to compensate for this.
                if (QDate::currentDate() != qCurrentDate)
                {
                    QDate newDate = QDate::currentDate();
                    i += (newDate.daysTo(qCurrentDate));
                    if (i < 0) 
                        i = 0;
                    qCurrentDate = newDate;
                }

                QString prevDate(qCurrentDate.addDays(i-1).toString());
                QString currDate(qCurrentDate.addDays(i).toString());

                VERBOSE(VB_GENERAL, ""); // add a space between days
                VERBOSE(VB_GENERAL, "Checking day @ " <<
                        QString("offset %1, date: %2").arg(i).arg(currDate));

                bool download_needed = false;

                if (refresh_all)
                {
                    VERBOSE(VB_GENERAL,
                            "Data Refresh needed because of --refresh-all");
                    download_needed = true;
                }
                else if ((i == 0 && refresh_today) || (i == 1 && refresh_tomorrow) ||
                         (i == 2 && refresh_second))
                {
                    // Always refresh if the user specified today/tomorrow/second.
                    if (refresh_today)
                    {
                        VERBOSE(VB_GENERAL,
                            "Data Refresh needed because user specified --refresh-today");
                    }
                    else if (refresh_second)
                    {
                        VERBOSE(VB_GENERAL,
                            "Data Refresh needed because user specified --refresh-second");
                    }
                    else
                    {
                        VERBOSE(VB_GENERAL,
                            "Data Refresh always needed for tomorrow");
                    }
                }
                else
                {
                    // Check to see if we already downloaded data for this date.
                    int chanCount = 0;         // Channels with data only
                    int previousDayCount = 0;
                    int currentDayCount = 0;

                    querystr = "SELECT c.chanid, COUNT(p.starttime) "
                               "FROM channel c "
                               "LEFT JOIN program p ON c.chanid = p.chanid "
                               "  AND starttime >= "
                                   "DATE_ADD(DATE_ADD(CURRENT_DATE(), "
                                   "INTERVAL '%1' DAY), INTERVAL '18' HOUR) "
                               "  AND starttime < DATE_ADD(CURRENT_DATE(), "
                                   "INTERVAL '%2' DAY) "
                               "WHERE c.sourceid = %3 AND c.xmltvid != '' "
                               "GROUP BY c.chanid;";

                    if (query.exec(querystr.arg(i-1).arg(i).arg((*it).id)) &&
                        query.isActive()) 
                    {
                        VERBOSE(VB_CHANNEL, QString(
                                "Checking program counts for day %1").arg(i-1));

                        while (query.next())
                        {
                            if (query.value(1).toInt() > 0)
                            {
                                chanCount++;
                                previousDayCount += query.value(1).toInt();
                            }
                            VERBOSE(VB_CHANNEL,
                                    QString("    chanid %1 -> %2 programs")
                                            .arg(query.value(0).toString())
                                            .arg(query.value(1).toInt()));
                        }

                        if (query.exec(querystr.arg(i).arg(i+1).arg((*it).id))
                                && query.isActive()) 
                        {
                            VERBOSE(VB_CHANNEL, QString("Checking program "
                                                "counts for day %1").arg(i));
                            while (query.next())
                            {
                                currentDayCount += query.value(1).toInt();
                                VERBOSE(VB_CHANNEL,
                                        QString("    chanid %1 -> %2 programs")
                                                .arg(query.value(0).toString())
                                                .arg(query.value(1).toInt()));
                            }
                        }
                        else
                        {
                            VERBOSE(VB_GENERAL, QString(
                                    "Data Refresh because we are unable to "
                                    "query the data for day %1 to "
                                    "determine if we have enough").arg(i));
                            download_needed = true;
                        }

                        if (currentDayCount == 0)
                        {
                            VERBOSE(VB_GENERAL, QString(
                                    "Data refresh needed because no data "
                                    "exists for day @ offset %1 from 6PM - "
                                    "midnight.").arg(i)); 
                            download_needed = true;
                        }
                        else if (previousDayCount == 0)
                        {
                            VERBOSE(VB_GENERAL, QString(
                                    "Data refresh needed because no data "
                                    "exists for day @ offset %1 from 6PM - "
                                    "midnight.  Unable to calculate how much "
                                    "we should have for the current day so "
                                    "a refresh is being forced.").arg(i-1)); 
                            download_needed = true;
                        }
                        else if (currentDayCount < (chanCount * 4))
                        {
                            VERBOSE(VB_GENERAL, QString(
                                    "Data Refresh needed because offset day %1 "
                                    "has less than 4 programs "
                                    "per channel for the 6PM - midnight "
                                    "time window for channels that "
                                    "normally have data. "
                                    "We want at least %2 programs, but only "
                                    "found %3").arg(i)
                                    .arg(chanCount * 4).arg(currentDayCount));
                            download_needed = true;
                        }
                        else if (currentDayCount < (previousDayCount / 2))
                        {
                            VERBOSE(VB_GENERAL, QString(
                                    "Data Refresh needed because offset day %1 "
                                    "has less than half the number of programs "
                                    "as the previous day for the 6PM - "
                                    "midnight time window. "
                                    "We want at least %2 programs, but only "
                                    "found %3").arg(i)
                                    .arg(previousDayCount / 2)
                                    .arg(currentDayCount));
                            download_needed = true;
                        }
                    }
                    else
                    {
                        VERBOSE(VB_GENERAL, QString(
                                "Data Refresh because we are unable to "
                                "query the data for day @ offset %1 to "
                                "determine how much we should have for "
                                "offset day %2.").arg(i-1).arg(i));
                        download_needed = true;
                    }
                }

                if (download_needed)
                {
                    VERBOSE(VB_IMPORTANT,
                            QString("Refreshing data for ") + currDate);
                    if (!grabData(*it, i, &qCurrentDate))
                    {
                        ++failures;
                        if (interrupted)
                        {
                            break;
                        }
                    }

                    if (endofdata)
                    {
                        VERBOSE(VB_GENERAL,
                            QString("Grabber is no longer returning program data, finishing"));
                        break;
                    }

                }
                else
                {
                    VERBOSE(VB_IMPORTANT,
                            QString("Data is already present for ") + currDate +
                                    ", skipping");
                }
            }
        }
        else
        {
            VERBOSE(VB_IMPORTANT,
                    QString("Grabbing XMLTV data using ") + xmltv_grabber +
                            " is not supported. You may need to upgrade to"
                            " the latest version of XMLTV.");
        }

        if (interrupted)
        {
            break;
        }

        query.prepare("SELECT MAX(endtime) FROM program p LEFT JOIN channel c "
                      "ON p.chanid=c.chanid WHERE c.sourceid= :SRCID "
                      "AND manualid = 0;");
        query.bindValue(":SRCID", (*it).id);
        query.exec();
        if (query.isActive() && query.size() > 0)
        {
            query.next();

            if (!query.isNull(0))
                GuideDataAfter = QDateTime::fromString(query.value(0).toString(),
                                                    Qt::ISODate);
        }

        if (GuideDataAfter == GuideDataBefore)
        {
            nonewdata++;
        }
    }

    if (only_update_channels && !need_post_grab_proc)
        return true;

    if (failures == 0)
    {
        if (nonewdata > 0 &&
            (total_sources != externally_handled))
            status = QString("mythfilldatabase ran, but did not insert "
                     "any new data into the Guide for %1 of %2 sources. "
                     "This can indicate a potential grabber failure.")
                     .arg(nonewdata)
                     .arg(total_sources);
        else
            status = "Successful.";

        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunStatus'")
                           .arg(status));
    }

    clearOldDBEntries();

    return (failures == 0);
}

ChanInfo *xawtvChannel(QString &id, QString &channel, QString &fine)
{
    ChanInfo *chaninfo = new ChanInfo;
    chaninfo->xmltvid = id;
    chaninfo->name = id;
    chaninfo->callsign = id;
    if (channel_preset)
        chaninfo->chanstr = id;
    else
        chaninfo->chanstr = channel;
    chaninfo->finetune = fine;
    chaninfo->freqid = channel;
    chaninfo->iconpath = "";
    chaninfo->tvformat = "Default";

    return chaninfo;
}

void readXawtvChannels(int id, QString xawrcfile)
{
    fstream fin(xawrcfile.ascii(), ios::in);
    if (!fin.is_open()) return;

    QValueList<ChanInfo> chanlist;

    QString xawid;
    QString channel;
    QString fine;

    string strLine;
    int nSplitPoint = 0;

    while(!fin.eof())
    {
        getline(fin,strLine);

        if ((strLine[0] != '#') && (!strLine.empty()))
        {
            if (strLine[0] == '[')
            {
                if ((nSplitPoint = strLine.find(']')) > 1)
                {
                    if ((xawid != "") && (channel != ""))
                    {
                        ChanInfo *chinfo = xawtvChannel(xawid, channel, fine);
                        chanlist.push_back(*chinfo);
                        delete chinfo;
                    }
                    xawid = strLine.substr(1, nSplitPoint - 1).c_str();
                    channel = "";
                    fine = "";
                }
            }
            else if ((nSplitPoint = strLine.find('=') + 1) > 0)
            {
                while (strLine.substr(nSplitPoint,1) == " ")
                { ++nSplitPoint; }

                if (!strncmp(strLine.c_str(), "channel", 7))
                {
                    channel = strLine.substr(nSplitPoint, 
                                             strLine.size()).c_str();
                }
                else if (!strncmp(strLine.c_str(), "fine", 4))
                {
                    fine = strLine.substr(nSplitPoint, strLine.size()).c_str();
                }
            }
        }
    }

    if ((xawid != "") && (channel != ""))
    {
        ChanInfo *chinfo = xawtvChannel(xawid, channel, fine);
        chanlist.push_back(*chinfo);
        delete chinfo;
    }

    handleChannels(id, &chanlist);
}

int fix_end_times(void)
{
    int count = 0;
    QString chanid, starttime, endtime, querystr;
    MSqlQuery query1(MSqlQuery::InitCon()), query2(MSqlQuery::InitCon());

    querystr = "SELECT chanid, starttime, endtime FROM program "
               "WHERE (DATE_FORMAT(endtime,'%H%i') = '0000') "
               "ORDER BY chanid, starttime;";

    if (!query1.exec(querystr))
    {
        VERBOSE(VB_IMPORTANT,
                QString("fix_end_times query failed: %1").arg(querystr));
        return -1;
    }

    while (query1.next())
    {
        starttime = query1.value(1).toString();
        chanid = query1.value(0).toString();
        endtime = query1.value(2).toString();

        querystr = QString("SELECT chanid, starttime, endtime FROM program "
                           "WHERE (DATE_FORMAT(starttime, '%%Y-%%m-%%d') = "
                           "'%1') AND chanid = '%2' "
                           "ORDER BY starttime LIMIT 1;")
                           .arg(endtime.left(10))
                           .arg(chanid);

        if (!query2.exec(querystr))
        {
            VERBOSE(VB_IMPORTANT,
                    QString("fix_end_times query failed: %1").arg(querystr));
            return -1;
        }

        if (query2.next() && (endtime != query2.value(1).toString()))
        {
            count++;
            endtime = query2.value(1).toString();
            querystr = QString("UPDATE program SET starttime = '%1', "
                               "endtime = '%2' WHERE (chanid = '%3' AND "
                               "starttime = '%4');")
                               .arg(starttime)
                               .arg(endtime)
                               .arg(chanid)
                               .arg(starttime);

            if (!query2.exec(querystr)) 
            {
                VERBOSE(VB_IMPORTANT,
                       QString("fix_end_times query failed: %1").arg(querystr));
                return -1;
            }
        }
    }

    return count;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv, false);
    int argpos = 1;
    int fromfile_id = 1;
    int fromfile_offset = 0;
    QString fromfile_name;
    bool from_xawfile = false;
    int fromxawfile_id = 1;
    QString fromxawfile_name;

    bool usingDataDirect = false, usingDataDirectLabs = false;
    bool grab_data = true;

    bool export_iconmap = false;
    bool import_iconmap = false;
    bool reset_iconmap = false;
    bool reset_iconmap_icons = false;
    QString import_icon_map_filename("iconmap.xml");
    QString export_icon_map_filename("iconmap.xml");

    bool update_icon_map = false;

    bool from_dd_file = false;
    int sourceid = -1;
    QString fromddfile_lineupid;

    while (argpos < a.argc())
    {
        // The manual and update flags should be mutually exclusive.
        if (!strcmp(a.argv()[argpos], "--manual"))
        {
            cout << "###\n";
            cout << "### Running in manual channel configuration mode.\n";
            cout << "### This will ask you questions about every channel.\n";
            cout << "###\n";
            interactive = true;
        }
        else if (!strcmp(a.argv()[argpos], "--preset"))
        {
            // For using channel preset values instead of channel numbers.
            cout << "###\n";
            cout << "### Running in preset channel configuration mode.\n";
            cout << "### This will assign channel ";
            cout << "preset numbers to every channel.\n";
            cout << "###\n";
            channel_preset = true;
        }
        else if (!strcmp(a.argv()[argpos], "--update"))
        {
            // For running non-destructive updates on the database for
            // users in xmltv zones that do not provide channel data.
            non_us_updating = true;
        }
        else if (!strcmp(a.argv()[argpos], "--no-delete"))
        {
            // Do not delete old programs from the database until 7 days old.
            no_delete = true;
        }
        else if (!strcmp(a.argv()[argpos], "--file"))
        {
            if (((argpos + 3) >= a.argc()) ||
                !strncmp(a.argv()[argpos + 1], "--", 2) ||
                !strncmp(a.argv()[argpos + 2], "--", 2) ||
                !strncmp(a.argv()[argpos + 3], "--", 2))
            {
                printf("missing or invalid parameters for --file option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            fromfile_id = atoi(a.argv()[++argpos]);
            fromfile_offset = atoi(a.argv()[++argpos]);
            fromfile_name = a.argv()[++argpos];

            if (!quiet)
                cout << "### bypassing grabbers, reading directly from file\n";
            from_file = true;
        }
        else if (!strcmp(a.argv()[argpos], "--dd-file"))
        {
            if (((argpos + 4) >= a.argc()) ||
                !strncmp(a.argv()[argpos + 1], "--", 2) ||
                !strncmp(a.argv()[argpos + 2], "--", 2) ||
                !strncmp(a.argv()[argpos + 3], "--", 2) ||
                !strncmp(a.argv()[argpos + 4], "--", 2))
            {
                printf("missing or invalid parameters for --dd-file option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            fromfile_id = atoi(a.argv()[++argpos]);
            fromfile_offset = atoi(a.argv()[++argpos]);
            fromddfile_lineupid = a.argv()[++argpos];
            fromfile_name = a.argv()[++argpos];

            if (!quiet)
                cout << "### bypassing grabbers, reading directly from file\n";
            from_dd_file = true;
        }
        else if (!strcmp(a.argv()[argpos], "--xawchannels"))
        {
            if (((argpos + 2) >= a.argc()) ||
                !strncmp(a.argv()[argpos + 1], "--", 2) ||
                !strncmp(a.argv()[argpos + 2], "--", 2))
            {
                printf("missing or invalid parameters for --xawchannels option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            fromxawfile_id = atoi(a.argv()[++argpos]);
            fromxawfile_name = a.argv()[++argpos];

            if (!quiet)
                 cout << "### reading channels from xawtv configfile\n";
            from_xawfile = true;
        }
        else if ((!strcmp(a.argv()[argpos], "--do-channel-updates")) ||
                 (!strcmp(a.argv()[argpos], "--do_channel_updates")))
        {
            channel_updates = true;
        }
        else if (!strcmp(a.argv()[argpos], "--remove-new-channels"))
        {
            remove_new_channels = true;
        }
        else if (!strcmp(a.argv()[argpos], "--do-not-filter-new-channels"))
        {
            filter_new_channels = false;
        }
        else if (!strcmp(a.argv()[argpos], "--graboptions"))
        {
            if (((argpos + 1) >= a.argc()))
            {
                printf("missing parameter for --graboptions option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            graboptions = QString(" ") + QString(a.argv()[++argpos]);
        }
        else if (!strcmp(a.argv()[argpos], "--sourceid"))
        {
            if (((argpos + 1) >= a.argc()))
            {
                printf("missing parameter for --sourceid option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            sourceid = QString(a.argv()[++argpos]).toInt();
        }
        else if (!strcmp(a.argv()[argpos], "--cardtype"))
        {
            if (!sourceid)
            {
                printf("--cardtype option must follow a --sourceid option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            if (((argpos + 1) >= a.argc()))
            {
                printf("missing parameter for --cardtype option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            cardtype = QString(a.argv()[++argpos]).stripWhiteSpace().upper();
        }
        else if (!strcmp(a.argv()[argpos], "--max-days"))
        {
            if (((argpos + 1) >= a.argc()))
            {
                printf("missing parameter for --max-days option\n");
                return FILLDB_EXIT_INVALID_CMDLINE;
            }

            maxDays = QString(a.argv()[++argpos]).toInt();

            if (maxDays < 1 || maxDays > 21)
            {
                printf("ignoring invalid parameter for --max-days\n");
                maxDays = 0;
            }
        }
        else if (!strcmp(a.argv()[argpos], "--refresh-today"))
        {
            refresh_today = true;
        }
        else if (!strcmp(a.argv()[argpos], "--dont-refresh-tomorrow"))
        {
            refresh_tomorrow = false;
        }
        else if (!strcmp(a.argv()[argpos], "--refresh-second"))
        {
            refresh_second = true;
        }
        else if (!strcmp(a.argv()[argpos], "--refresh-all"))
        {
            refresh_all = true;
        }
        else if (!strcmp(a.argv()[argpos], "--dont-refresh-tba"))
        {
            refresh_tba = false;
        }
        else if (!strcmp(a.argv()[argpos], "--only-update-channels"))
        {
            only_update_channels = true;
        }
        else if (!strcmp(a.argv()[argpos],"-v") ||
                 !strcmp(a.argv()[argpos],"--verbose"))
        {
            if (a.argc()-1 > argpos)
            {
                if (parse_verbose_arg(a.argv()[argpos+1]) ==
                        GENERIC_EXIT_INVALID_CMDLINE)
                    return GENERIC_EXIT_INVALID_CMDLINE;

                ++argpos;
            } 
            else
            {
                cerr << "Missing argument to -v/--verbose option\n";
                return GENERIC_EXIT_INVALID_CMDLINE;
            }
        }
#if 0
        else if (!strcmp(a.argv()[argpos], "--dd-grab-all"))
        {
            dd_grab_all = true;
            refresh_today = false;
            refresh_tomorrow = false;
            refresh_second = false;
        }
#endif
        else if (!strcmp(a.argv()[argpos], "--quiet"))
        {
             quiet = true;
             print_verbose_messages = VB_NONE;
        }
        else if (!strcmp(a.argv()[argpos], "--mark-repeats"))
        {
             mark_repeats = true;
        }
        else if (!strcmp(a.argv()[argpos], "--nomark-repeats"))
        {
             mark_repeats = false;
        }
        else if (!strcmp(a.argv()[argpos], "--export-icon-map"))
        {
            export_iconmap = true;
            grab_data = false;

            if ((argpos + 1) >= a.argc() ||
                    !strncmp(a.argv()[argpos + 1], "--", 2))
            {
                if (!isatty(fileno(stdout)))
                {
                    quiet = true;
                    export_icon_map_filename = "-";
                }
            }
            else
            {
                export_icon_map_filename = a.argv()[++argpos];
            }
        }
        else if (!strcmp(a.argv()[argpos], "--import-icon-map"))
        {
            import_iconmap = true;
            grab_data = false;

            if ((argpos + 1) >= a.argc() ||
                    !strncmp(a.argv()[argpos + 1], "--", 2))
            {
                if (!isatty(fileno(stdin)))
                {
                    import_icon_map_filename = "-";
                }
            }
            else
            {
                import_icon_map_filename = a.argv()[++argpos];
            }
        }
        else if (!strcmp(a.argv()[argpos], "--update-icon-map"))
        {
            update_icon_map = true;
            grab_data = false;
        }
        else if (!strcmp(a.argv()[argpos], "--reset-icon-map"))
        {
            reset_iconmap = true;
            grab_data = false;

            if ((argpos + 1) < a.argc() &&
                    strncmp(a.argv()[argpos + 1], "--", 2))
            {
                ++argpos;
                if (QString(a.argv()[argpos]) == "all")
                {
                    reset_iconmap_icons = true;
                }
                else
                {
                    cerr << "Unknown icon group '" << a.argv()[argpos]
                            << "' for --reset-icon-map option" << endl;
                    return FILLDB_EXIT_UNKNOWN_ICON_GROUP;
                }
            }
        }
        else if (!strcmp(a.argv()[argpos], "-h") ||
                 !strcmp(a.argv()[argpos], "--help"))
        {
            cout << "usage:\n";
            cout << "--manual\n";
            cout << "   Run in manual channel configuration mode\n";
            cout << "   This will ask you questions about every channel\n";
            cout << "\n";
            cout << "--update\n";
            cout << "   For running non-destructive updates on the database for\n";
            cout << "   users in xmltv zones that do not provide channel data\n";
            cout << "\n";
            cout << "--preset\n";
            cout << "   Use it in case that you want to assign a preset number for\n";
            cout << "   each channel, useful for non US countries where people\n";
            cout << "   are used to assigning a sequenced number for each channel, i.e.:\n";
            cout << "   1->TVE1(S41), 2->La 2(SE18), 3->TV3(21), 4->Canal 33(60)...\n";
            cout << "\n";
            cout << "--no-delete\n";
            cout << "   Do not delete old programs from the database until 7 days old.\n";
            cout << "\n";
            cout << "--file <sourceid> <offset> <xmlfile>\n";
            cout << "   Bypass the grabbers and read data directly from a file\n";
            cout << "   <sourceid> = number for the video source to use with this file\n";
            cout << "   <offset>   = days from today that xmlfile defines\n";
            cout << "                (-1 means to replace all data, up to 10 days)\n";
            cout << "   <xmlfile>  = file to read\n";
            cout << "\n";
            cout << "--dd-file <sourceid> <offset> <lineupid> <xmlfile>\n";
            cout << "   <sourceid> = see --file\n";
            cout << "   <offset>   = see --file\n";
            cout << "   <lineupid> = the lineup id\n";
            cout << "   <xmlfile>  = see --file\n";
            cout << "\n";
            cout << "--xawchannels <sourceid> <xawtvrcfile>\n";
            cout << "   (--manual flag works in combination with this)\n";
            cout << "   Read channels as defined in xawtvrc file given\n";
            cout << "   <sourceid>    = cardinput\n";
            cout << "   <xawtvrcfile> = file to read\n";
            cout << "\n";
            cout << "--do-channel-updates\n";
            cout << "   When using DataDirect, ask mythfilldatabase to\n";
            cout << "   overwrite channel names, frequencies, etc. with the\n";
            cout << "   values available from the data source. This will \n";
            cout << "   override custom channel names, which is why it is\n";
            cout << "   off by default.\n";
            cout << "\n";
            cout << "--remove-new-channels\n";
            cout << "   When using DataDirect, ask mythfilldatabase to\n";
            cout << "   remove new channels (those not in the database)\n";
            cout << "   from the DataDirect lineup.  These channels are\n";
            cout << "   removed from the lineup as if you had done so\n";
            cout << "   via the DataDirect website's Lineup Wizard, but\n";
            cout << "   may be re-added manually and incorporated into\n";
            cout << "   MythTV by running mythfilldatabase without this\n";
            cout << "   option.  New channels are automatically removed\n";
            cout << "   for DVB and HDTV sources that use DataDirect.\n";
            cout << "\n";
            cout << "--do-not-filter-new-channels\n";
            cout << "   Normally MythTV tries to avoid adding ATSC channels\n";
            cout << "   to NTSC channel lineups. This option restores the\n";
            cout << "   behaviour of adding every channel in the downloaded\n";
            cout << "   channel lineup to MythTV's lineup, in case MythTV's\n";
            cout << "   smarts fail you.\n";
            cout << "\n";
            cout << "--graboptions <\"options\">\n";
            cout << "   Pass options to grabber\n";
            cout << "\n";
            cout << "--sourceid <number>\n";
            cout << "   Only refresh data for sourceid given\n";
            cout << "\n";
            cout << "--max-days <number>\n";
            cout << "   Force the maximum number of days, counting today,\n";
            cout << "   for the grabber to check for future listings\n";
            cout << "--only-update-channels\n";
            cout << "   Get as little listings data as possible to update channels\n";
            cout << "--refresh-today\n";
            cout << "--refresh-second\n";
            cout << "--refresh-all\n";
            cout << "   (Only valid for selected grabbers: e.g. DataDirect)\n";
            cout << "   Force a refresh today or two days (or every day) from now,\n";
            cout << "   to catch the latest changes\n";
            cout << "--dont-refresh-tomorrow\n";
            cout << "   Tomorrow will always be refreshed unless this argument is used\n";
            cout << "--dont-refresh-tba\n";
            cout << "   \"To be announced\" programs will always be refreshed \n";
            cout << "   unless this argument is used\n";
            cout << "\n";
            cout << "--export-icon-map [<filename>]\n";
            cout << "   Exports your current icon map to <filename> (default: "
                    << export_icon_map_filename << ")\n";
            cout << "--import-icon-map [<filename>]\n";
            cout << "   Imports an icon map from <filename> (default: " <<
                    import_icon_map_filename << ")\n";
            cout << "--update-icon-map\n";
            cout << "   Updates icon map icons only\n";
            cout << "--reset-icon-map [all]\n";
            cout << "   Resets your icon map (pass all to reset channel icons as well)\n";
            cout << "\n";
            cout << "--mark-repeats\n";
            cout << "   Marks any programs with a OriginalAirDate earlier\n";
            cout << "   than their start date as a repeat\n";
            cout << "\n";
            cout << "-v or --verbose debug-level\n";
            cout << "   Use '-v help' for level info\n";
            cout << "\n";

#if 0
            cout << "--dd-grab-all\n";
            cout << "   The DataDirect grabber will grab all available data\n";
#endif
            cout << "--help\n";
            cout << "   This text\n";
            cout << "\n";
            cout << "\n";
            cout << "  --manual and --update can not be used together.\n";
            cout << "\n";
            return FILLDB_EXIT_INVALID_CMDLINE;
        }
        else
        {
            fprintf(stderr, "illegal option: '%s' (use --help)\n",
                    a.argv()[argpos]);
            return FILLDB_EXIT_INVALID_CMDLINE;
        }

        ++argpos;
    }

    gContext = NULL;
    gContext = new MythContext(MYTH_BINARY_VERSION);
    if (!gContext->Init(false))
    {
        VERBOSE(VB_IMPORTANT, "Failed to init MythContext, exiting.");
        return FILLDB_EXIT_NO_MYTHCONTEXT;
    }

    gContext->LogEntry("mythfilldatabase", LP_INFO,
                       "Listings Download Started", "");
    
    
    if (!grab_data)
    {
    }
    else if (from_xawfile)
    {
        readXawtvChannels(fromxawfile_id, fromxawfile_name);
    }
    else if (from_file)
    {
        QString status = "currently running.";
        QDateTime GuideDataBefore, GuideDataAfter;

        MSqlQuery query(MSqlQuery::InitCon());
        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunStart'")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));

        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunStatus'")
                           .arg(status));

        query.exec(QString("SELECT MAX(endtime) FROM program;"));
        if (query.isActive() && query.size() > 0)
        {
            query.next();

            if (!query.isNull(0))
                GuideDataBefore = QDateTime::fromString(query.value(0).toString(),
                                                    Qt::ISODate);
        }

        if (!grabDataFromFile(fromfile_id, fromfile_name))
            return FILLDB_EXIT_GRAB_DATA_FAILED;

        clearOldDBEntries();

        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunEnd'")
                          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm")));

        query.exec(QString("SELECT MAX(endtime) FROM program;"));
        if (query.isActive() && query.size() > 0)
        {
            query.next();

            if (!query.isNull(0))
                GuideDataAfter = QDateTime::fromString(query.value(0).toString(),
                                                   Qt::ISODate);
        }

        if (GuideDataAfter == GuideDataBefore)
            status = "mythfilldatabase ran, but did not insert "
                     "any new data into the Guide.  This can indicate a "
                     "potential problem with the XML file used for the update.";
        else
            status = "Successful.";

        query.exec(QString("UPDATE settings SET data ='%1' "
                           "WHERE value='mythfilldatabaseLastRunStatus'")
                           .arg(status));
    }
    else if (from_dd_file)
    {
        grabDataFromDDFile(fromfile_id, fromfile_offset, fromfile_name,
                fromddfile_lineupid);
        clearOldDBEntries();
    }
    else
    {
        QValueList<Source> sourcelist;

        MSqlQuery sourcequery(MSqlQuery::InitCon());
        QString where = "";

        if (sourceid != -1)
        {
            VERBOSE(VB_GENERAL,
                    QString("Running for sourceid %1 ONLY because --sourceid "
                            "was given on command-line").arg(sourceid));
            where = QString("WHERE sourceid = %1").arg(sourceid);
        }

        QString querystr = QString("SELECT sourceid,name,xmltvgrabber,userid,"
                                   "password,lineupid "
                                   "FROM videosource ") + where +
                                   QString(" ORDER BY sourceid;");
        sourcequery.exec(querystr);

        if (sourcequery.isActive())
        {
             if (sourcequery.size() > 0)
             {
                  while (sourcequery.next())
                  {
                       Source newsource;

                       newsource.id = sourcequery.value(0).toInt();
                       newsource.name = sourcequery.value(1).toString();
                       newsource.xmltvgrabber = sourcequery.value(2).toString();
                       newsource.userid = sourcequery.value(3).toString();
                       newsource.password = sourcequery.value(4).toString();
                       newsource.lineupid = sourcequery.value(5).toString();

                       newsource.xmltvgrabber_baseline = false;
                       newsource.xmltvgrabber_manualconfig = false;
                       newsource.xmltvgrabber_cache = false;
                       newsource.xmltvgrabber_prefmethod = "";

                       sourcelist.append(newsource);
                       usingDataDirect |=
                           is_grabber_datadirect(newsource.xmltvgrabber);
                       usingDataDirectLabs |=
                           is_grabber_labs(newsource.xmltvgrabber);
                  }
             }
             else
             {
                  VERBOSE(VB_IMPORTANT,
                          "There are no channel sources defined, did you run "
                          "the setup program?");
                  gContext->LogEntry("mythfilldatabase", LP_CRITICAL,
                                     "No channel sources defined",
                                     "Could not find any defined channel "
                                     "sources - did you run the setup "
                                     "program?");
                  return FILLDB_EXIT_NO_CHAN_SRC;
             }
        }
        else
        {
             MythContext::DBError("loading channel sources", sourcequery);
             return FILLDB_EXIT_DB_ERROR;
        }
    
        if (!fillData(sourcelist))
        {
             VERBOSE(VB_IMPORTANT, "Failed to fetch some program info");
             gContext->LogEntry("mythfilldatabase", LP_WARNING,
                                "Failed to fetch some program info", "");
        }
        else
            VERBOSE(VB_IMPORTANT, "Data fetching complete.");
    }

    if (only_update_channels && !need_post_grab_proc)
    {
        delete gContext;
        return FILLDB_EXIT_OK;
    }

    if (reset_iconmap)
    {
        ResetIconMap(reset_iconmap_icons);
    }

    if (import_iconmap)
    {
        ImportIconMap(import_icon_map_filename);
    }

    if (export_iconmap)
    {
        ExportIconMap(export_icon_map_filename);
    }

    if (update_icon_map)
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.exec("SELECT sourceid FROM videosource ORDER BY sourceid;");
        if (query.isActive() && query.size() > 0)
        {
            while (query.next())
            {
                UpdateSourceIcons(query.value(0).toInt());
            }
        }
    }

    if (grab_data)
    {
        VERBOSE(VB_GENERAL, "Adjusting program database end times.");
        int update_count = fix_end_times();
        if (update_count == -1)
            VERBOSE(VB_IMPORTANT, "fix_end_times failed!");
        else if (!quiet)
            VERBOSE(VB_GENERAL,
                    QString("    %1 replacements made").arg(update_count));

        gContext->LogEntry("mythfilldatabase", LP_INFO,
                           "Listings Download Finished", "");
    }

    if (grab_data)
    {
        VERBOSE(VB_GENERAL, "Marking generic episodes.");

        MSqlQuery query(MSqlQuery::InitCon());
        query.exec("UPDATE program SET generic = 1 WHERE "
            "((programid = '' AND subtitle = '' AND description = '') OR "
            " (programid <> '' AND category_type = 'series' AND "
            "  program.programid LIKE '%0000'));");

        VERBOSE(VB_GENERAL,
                QString("    Found %1").arg(query.numRowsAffected()));
    }

    if (mark_repeats)
    {
        VERBOSE(VB_GENERAL, "Marking repeats.");
       
        int newEpiWindow = gContext->GetNumSetting( "NewEpisodeWindow", 14);
        
        MSqlQuery query(MSqlQuery::InitCon());
        query.exec( QString( "UPDATE program SET previouslyshown = 1 "
                    "WHERE previouslyshown = 0 "
                    "AND originalairdate is not null "
                    "AND (to_days(starttime) - to_days(originalairdate)) > %1;")
                    .arg(newEpiWindow));
        
        VERBOSE(VB_GENERAL,
                QString("    Found %1").arg(query.numRowsAffected()));
            
        VERBOSE(VB_GENERAL, "Unmarking new episode rebroadcast repeats.");
        query.exec( QString( "UPDATE program SET previouslyshown = 0 "
                             "WHERE previouslyshown = 1 "
                             "AND originalairdate is not null "
                             "AND (to_days(starttime) - to_days(originalairdate)) <= %1;")
                             .arg(newEpiWindow));             
    
        VERBOSE(VB_GENERAL,
                QString("    Found %1").arg(query.numRowsAffected()));
    }

    // Mark first and last showings

    if (grab_data)
    {
        MSqlQuery updt(MSqlQuery::InitCon());
        updt.exec("UPDATE program SET first = 0, last = 0;");

        VERBOSE(VB_GENERAL, "Marking episode first showings.");

        MSqlQuery query(MSqlQuery::InitCon());
        query.exec("SELECT MIN(starttime),programid FROM program "
                   "WHERE programid > '' GROUP BY programid;");

        if (query.isActive() && query.size() > 0)
        {
            while(query.next())
            {
                updt.prepare("UPDATE program set first = 1 "
                             "WHERE starttime = :STARTTIME "
                             "  AND programid = :PROGRAMID;");
                updt.bindValue(":STARTTIME", query.value(0).toDateTime());
                updt.bindValue(":PROGRAMID", query.value(1).toString());
                updt.exec();
            }
        }
        int found = query.numRowsAffected();

        query.exec("SELECT MIN(starttime),title,subtitle,description "
                   "FROM program WHERE programid = '' "
                   "GROUP BY title,subtitle,description;");

        if (query.isActive() && query.size() > 0)
        {
            while(query.next())
            {
                updt.prepare("UPDATE program set first = 1 "
                             "WHERE starttime = :STARTTIME "
                             "  AND title = :TITLE "
                             "  AND subtitle = :SUBTITLE "
                             "  AND description = :DESCRIPTION");
                updt.bindValue(":STARTTIME", query.value(0).toDateTime());
                updt.bindValue(":TITLE", query.value(1).toString());
                updt.bindValue(":SUBTITLE", query.value(2).toString());
                updt.bindValue(":DESCRIPTION", query.value(3).toString());
                updt.exec();
            }
        }
        found += query.numRowsAffected();
        VERBOSE(VB_GENERAL, QString("    Found %1").arg(found));

        VERBOSE(VB_GENERAL, "Marking episode last showings.");

        query.exec("SELECT MAX(starttime),programid FROM program "
                   "WHERE programid > '' GROUP BY programid;");

        if (query.isActive() && query.size() > 0)
        {
            while(query.next())
            {
                updt.prepare("UPDATE program set last = 1 "
                             "WHERE starttime = :STARTTIME "
                             "  AND programid = :PROGRAMID;");
                updt.bindValue(":STARTTIME", query.value(0).toDateTime());
                updt.bindValue(":PROGRAMID", query.value(1).toString());
                updt.exec();
            }
        }
        found = query.numRowsAffected();

        query.exec("SELECT MAX(starttime),title,subtitle,description "
                   "FROM program WHERE programid = '' "
                   "GROUP BY title,subtitle,description;");

        if (query.isActive() && query.size() > 0)
        {
            while(query.next())
            {
                updt.prepare("UPDATE program set last = 1 "
                             "WHERE starttime = :STARTTIME "
                             "  AND title = :TITLE "
                             "  AND subtitle = :SUBTITLE "
                             "  AND description = :DESCRIPTION");
                updt.bindValue(":STARTTIME", query.value(0).toDateTime());
                updt.bindValue(":TITLE", query.value(1).toString());
                updt.bindValue(":SUBTITLE", query.value(2).toString());
                updt.bindValue(":DESCRIPTION", query.value(3).toString());
                updt.exec();
            }
        }
        found += query.numRowsAffected();
        VERBOSE(VB_GENERAL, QString("    Found %1").arg(found));
    }

    if (1) // limit MSqlQuery's lifetime
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.exec( "SELECT count(previouslyshown) FROM program WHERE previouslyshown = 1;");
        if (query.isActive() && query.size() > 0)
        {
            query.next();
            if (query.value(0).toInt() != 0)
                query.exec("UPDATE settings SET data = '1' WHERE value = 'HaveRepeats';");
            else
                query.exec("UPDATE settings SET data = '0' WHERE value = 'HaveRepeats';");
        }
    }

    if ((usingDataDirect) &&
        (gContext->GetNumSetting("MythFillGrabberSuggestsTime", 1)))
    {
        ddprocessor.GrabNextSuggestedTime();
    }

    if (usingDataDirectLabs ||
        !gContext->GetNumSetting("MythFillFixProgramIDsHasRunOnce", 0))
    {
        DataDirectProcessor::FixProgramIDs();
    }

    VERBOSE(VB_IMPORTANT, "\n"
            "===============================================================\n"
            "| Attempting to contact the master backend for rescheduling.  |\n"
            "| If the master is not running, rescheduling will happen when |\n"
            "| the master backend is restarted.                            |\n"
            "===============================================================");

    if (grab_data || mark_repeats)
        ScheduledRecording::signalChange(-1);

    RemoteSendMessage("CLEAR_SETTINGS_CACHE");

    delete gContext;

    VERBOSE(VB_IMPORTANT, "mythfilldatabase run complete.");

    return FILLDB_EXIT_OK;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
