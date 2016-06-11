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

#include "ampache_browser/settings.h"
#include "ampache_browser/ampache_browser.h"
#include "ampache_browser/qt_application.h"

using namespace std;
using namespace placeholders;
using namespace ampache_browser;



class AmpacheBrowserPlugin: public GeneralPlugin {

public:
    static const char about[];

    static const PluginInfo pluginInfo;

    AmpacheBrowserPlugin(): GeneralPlugin(pluginInfo, false) {
    }

    bool init() override;

    void cleanup() override;

    void* get_qt_widget() override;

private:
    const char* SETTINGS_SECTION = "ampache_browser";
    const char* AUDACIOUS_SETTINGS_SECTION = "audacious";

    unique_ptr<QtApplication> myQtApplication;
    AmpacheBrowser* myAmpacheBrowser = nullptr;
    Settings* mySettings = nullptr;

    void onAmpacheBrowserPlay(vector<string> trackUrls);
    void onAmpacheBrowserCreatePlaylist(vector<string> trackUrls);
    void onAmpacheBrowserAddToPlaylist(vector<string> trackUrls);

    void onSettingsChanged();

    void onFinished();

    void networkRequest(const std::string& url, QtApplication::NetworkRequestCb& networkRequestCb);

    static Index<PlaylistAddItem> createPlaylistItems(const vector<string>& trackUrls);
    static int getVerbosity();
};



void onVfsAsyncFileGetContentsCb(const char* url, const Index<char>& buffer, void* userData) {
    auto& callback = *reinterpret_cast<QtApplication::NetworkRequestCb*>(userData);
    callback(url, vector<char>(buffer.begin(), buffer.end()));
}



const char AmpacheBrowserPlugin::about[] =
    N_("Ampache Browser\n\n"
       "Ampache client for Audacious.\n\n"
       "License: GNU GPLv3\n"
       "Copyright (C) Róbert Čerňanský\n");



const PluginInfo AmpacheBrowserPlugin::pluginInfo = {
    N_("Ampache Browser"),
    PACKAGE,
    about,
    nullptr,
    PluginQtOnly
};



bool AmpacheBrowserPlugin::init() {
    myQtApplication = unique_ptr<QtApplication>{new QtApplication{}};
    myQtApplication->setNetworkRequestFunction(bind(&AmpacheBrowserPlugin::networkRequest, this, _1, _2));

    mySettings = &myQtApplication->getSettings();
    mySettings->setBool(Settings::USE_DEMO_SERVER, aud_get_bool(SETTINGS_SECTION, Settings::USE_DEMO_SERVER.c_str()));
    mySettings->setString(Settings::SERVER_URL, string{aud_get_str(SETTINGS_SECTION, Settings::SERVER_URL.c_str())});
    mySettings->setString(Settings::USER_NAME, string{aud_get_str(SETTINGS_SECTION, Settings::USER_NAME.c_str())});
    mySettings->setString(Settings::PASSWORD_HASH,
        string{aud_get_str(SETTINGS_SECTION, Settings::PASSWORD_HASH.c_str())});
    mySettings->setInt(Settings::LOGGING_VERBOSITY, getVerbosity());

    mySettings->connectChanged(bind(&AmpacheBrowserPlugin::onSettingsChanged, this));

    myAmpacheBrowser = &myQtApplication->getAmpacheBrowser();
    myAmpacheBrowser->connectPlay(bind(&AmpacheBrowserPlugin::onAmpacheBrowserPlay, this, _1));
    myAmpacheBrowser->connectCreatePlaylist(bind(&AmpacheBrowserPlugin::onAmpacheBrowserCreatePlaylist, this, _1));
    myAmpacheBrowser->connectAddToPlaylist(bind(&AmpacheBrowserPlugin::onAmpacheBrowserAddToPlaylist, this, _1));

    return GeneralPlugin::init();
}



void AmpacheBrowserPlugin::cleanup() {
    // if the body of onFinished method would be defined here (as anonymous function), destroying of myQtApplication
    // would invalidate the captured 'this' pointer which would cause segfault when accessing instance variables later
    // on; therefore onFinished cannot be defined as anonymous
    myQtApplication->finishRequest(bind(&AmpacheBrowserPlugin::onFinished, this));
}



void* AmpacheBrowserPlugin::get_qt_widget() {
    myQtApplication->run();
    return myQtApplication->getMainWidget();
}



void AmpacheBrowserPlugin::onAmpacheBrowserPlay(vector<string> trackUrls) {
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), true);
}



void AmpacheBrowserPlugin::onAmpacheBrowserCreatePlaylist(vector<string> trackUrls) {
    aud_playlist_new();
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), true);
}



void AmpacheBrowserPlugin::onAmpacheBrowserAddToPlaylist(vector<string> trackUrls) {
    aud_playlist_entry_insert_batch(aud_playlist_get_active(), -1, move(createPlaylistItems(trackUrls)), false);
}



void AmpacheBrowserPlugin::onSettingsChanged() {
    aud_set_bool(SETTINGS_SECTION, Settings::USE_DEMO_SERVER.c_str(), mySettings->getBool(Settings::USE_DEMO_SERVER));
    aud_set_str(SETTINGS_SECTION, Settings::SERVER_URL.c_str(), mySettings->getString(Settings::SERVER_URL).c_str());
    aud_set_str(SETTINGS_SECTION, Settings::USER_NAME.c_str(), mySettings->getString(Settings::USER_NAME).c_str());
    aud_set_str(SETTINGS_SECTION, Settings::PASSWORD_HASH.c_str(),
        mySettings->getString(Settings::PASSWORD_HASH).c_str());
}



void AmpacheBrowserPlugin::onFinished() {
    AUDINFO("Finishing.\n");
    myQtApplication = nullptr;
}



void AmpacheBrowserPlugin::networkRequest(const string& url, QtApplication::NetworkRequestCb& networkRequestCb) {
    vfs_async_file_get_contents(url.c_str(), onVfsAsyncFileGetContentsCb, &networkRequestCb);
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
