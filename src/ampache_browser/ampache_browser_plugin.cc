// ampache_browser_plugin.cc
//
// Project: Ampache Browser Audacious Plugin
// License: GNU GPLv3
//
// Copyright (C) 2015 - 2016 Róbert Čerňanský



#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/playlist.h>
#include <libaudcore/vfs_async.h>

#include <ampache_browser/settings.h>
#include <ampache_browser/ampache_browser.h>
#include <ampache_browser/application_qt.h>

using namespace std;
using namespace placeholders;
using namespace ampache_browser;



class AmpacheBrowserPlugin: public GeneralPlugin {

public:
    static const char about[];

    static constexpr PluginInfo pluginInfo = {
        N_("Ampache Browser"),
        PACKAGE,
        about,
        nullptr,
        PluginQtOnly
    };

    constexpr AmpacheBrowserPlugin(): GeneralPlugin(pluginInfo, false) {
    }

    bool init() override;

    void cleanup() override;

    void* get_qt_widget() override;

private:
    const char* SETTINGS_SECTION = "ampache_browser";
    const char* AUDACIOUS_SETTINGS_SECTION = "audacious";

    unique_ptr<ApplicationQt> myApplicationQt;
    AmpacheBrowser* myAmpacheBrowser = nullptr;
    Settings* mySettings = nullptr;

    void onAmpacheBrowserPlay(const vector<string>& trackUrls);
    void onAmpacheBrowserCreatePlaylist(const vector<string>& trackUrls);
    void onAmpacheBrowserAddToPlaylist(const vector<string>& trackUrls);

    void onSettingsChanged();

    static Index<PlaylistAddItem> createPlaylistItems(const vector<string>& trackUrls);
    static int getVerbosity();
};



bool isInitialized = false;



void onVfsAsyncFileGetContentsCb(const char* url, const Index<char>& buffer, void* userData) {
    if (isInitialized) {
        auto& callback = *reinterpret_cast<ApplicationQt::NetworkRequestCb*>(userData);
        callback(url, buffer.begin(), buffer.len());
    } else {
        AUDDBG("Ignoring network request result for URL '%s'.\n", url);
    }
}



void networkRequest(const string& url, ApplicationQt::NetworkRequestCb& networkRequestCb) {
    vfs_async_file_get_contents(url.c_str(), onVfsAsyncFileGetContentsCb, &networkRequestCb);
}



const char AmpacheBrowserPlugin::about[] =
    N_("Ampache Browser\n\n"
       "Ampache client for Audacious.\n\n"
       "License: GNU GPLv3\n"
       "Copyright (C) Róbert Čerňanský\n");



bool AmpacheBrowserPlugin::init() {
    myApplicationQt = unique_ptr<ApplicationQt>{new ApplicationQt{}};
    myApplicationQt->setNetworkRequestFunction(networkRequest);

    mySettings = &myApplicationQt->getSettings();
    mySettings->setBool(Settings::USE_DEMO_SERVER, aud_get_bool(SETTINGS_SECTION, Settings::USE_DEMO_SERVER.c_str()));
    mySettings->setString(Settings::SERVER_URL, string{aud_get_str(SETTINGS_SECTION, Settings::SERVER_URL.c_str())});
    mySettings->setString(Settings::USER_NAME, string{aud_get_str(SETTINGS_SECTION, Settings::USER_NAME.c_str())});
    mySettings->setString(Settings::PASSWORD_HASH,
        string{aud_get_str(SETTINGS_SECTION, Settings::PASSWORD_HASH.c_str())});
    mySettings->setInt(Settings::LOGGING_VERBOSITY, getVerbosity());

    mySettings->connectChanged(bind(&AmpacheBrowserPlugin::onSettingsChanged, this));

    myAmpacheBrowser = &myApplicationQt->getAmpacheBrowser();
    myAmpacheBrowser->connectPlay(bind(&AmpacheBrowserPlugin::onAmpacheBrowserPlay, this, _1));
    myAmpacheBrowser->connectCreatePlaylist(bind(&AmpacheBrowserPlugin::onAmpacheBrowserCreatePlaylist, this, _1));
    myAmpacheBrowser->connectAddToPlaylist(bind(&AmpacheBrowserPlugin::onAmpacheBrowserAddToPlaylist, this, _1));

    isInitialized = true;

    return true;
}



void AmpacheBrowserPlugin::cleanup() {
    isInitialized = false;
    myApplicationQt = nullptr;
}



void* AmpacheBrowserPlugin::get_qt_widget() {
    myApplicationQt->run();
    return myApplicationQt->getMainWidget();
}



void AmpacheBrowserPlugin::onAmpacheBrowserPlay(const vector<string>& trackUrls) {
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), true);
}



void AmpacheBrowserPlugin::onAmpacheBrowserCreatePlaylist(const vector<string>& trackUrls) {
    aud_playlist_new();
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), true);
}



void AmpacheBrowserPlugin::onAmpacheBrowserAddToPlaylist(const vector<string>& trackUrls) {
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), false);
}



void AmpacheBrowserPlugin::onSettingsChanged() {
    aud_set_bool(SETTINGS_SECTION, Settings::USE_DEMO_SERVER.c_str(), mySettings->getBool(Settings::USE_DEMO_SERVER));
    aud_set_str(SETTINGS_SECTION, Settings::SERVER_URL.c_str(), mySettings->getString(Settings::SERVER_URL).c_str());
    aud_set_str(SETTINGS_SECTION, Settings::USER_NAME.c_str(), mySettings->getString(Settings::USER_NAME).c_str());
    aud_set_str(SETTINGS_SECTION, Settings::PASSWORD_HASH.c_str(),
        mySettings->getString(Settings::PASSWORD_HASH).c_str());
}



Index<PlaylistAddItem> AmpacheBrowserPlugin::createPlaylistItems(const vector<string>& trackUrls) {
    Index<PlaylistAddItem> playlistAddItems;
    for (auto& trackUrl: trackUrls) {
        Tuple tuple;
        playlistAddItems.append(String{trackUrl.c_str()}, move(tuple), nullptr);
    }
    return playlistAddItems;
}



int AmpacheBrowserPlugin::getVerbosity() {
    int verbosity = 3;

    auto verbosityEnv = getenv("AMPACHE_BROWSER_PLUGIN_VERBOSITY");
    if (verbosityEnv != nullptr) {
        try {
            verbosity = stoi(verbosityEnv);
        } catch (const invalid_argument&) {
        } catch (const out_of_range&) {
        }
    }

    return verbosity;
}



EXPORT AmpacheBrowserPlugin aud_plugin_instance;
