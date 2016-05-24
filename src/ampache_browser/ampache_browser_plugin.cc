// ampache_browser_plugin.cc
//
// Project: Ampache Browser Audacious Plugin
// License: GNU GPLv3
//
// Copyright (C) 2015 - 2016 Róbert Čerňanský



#define AUD_PLUGIN_QT_ONLY

#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/playlist.h>

#include "ampache_browser/settings.h"
#include "ampache_browser/ampache_browser.h"
#include "ampache_browser/qt_application.h"

using namespace std;
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
    const string SETTINGS_SECTION = "ampache_browser";

    unique_ptr<QtApplication> myQtApplication;
    AmpacheBrowser* myAmpacheBrowser = nullptr;
    Settings* mySettings = nullptr;

    void onAmpacheBrowserPlay(vector<string> trackUrls);
    void onAmpacheBrowserCreatePlaylist(vector<string> trackUrls);
    void onAmpacheBrowserAddToPlaylist(vector<string> trackUrls);

    void onSettingsChanged();

    void onFinished();

    static Index<PlaylistAddItem> createPlaylistItems(const vector<string>& trackUrls);
    static int getVerbosity();
};



const char AmpacheBrowserPlugin::about[] =
    N_("Ampache Browser\n\n"
        "Ampache client for Audacious.\n\n"
        "License: GNU GPLv3\n"
        "Copyright (C) Róbert Čerňanský\n");



const PluginInfo AmpacheBrowserPlugin::pluginInfo = {
    N_("Ampache Browser"),
    PACKAGE,
    about,
    nullptr
};



bool AmpacheBrowserPlugin::init() {
    myQtApplication = unique_ptr<QtApplication>{new QtApplication{}};

    mySettings = &myQtApplication->getSettings();
    mySettings->setBool(Settings::USE_DEMO_SERVER,
        aud_get_bool(SETTINGS_SECTION.c_str(), Settings::USE_DEMO_SERVER.c_str()));
    mySettings->setString(Settings::SERVER_URL,
        string{aud_get_str(SETTINGS_SECTION.c_str(), Settings::SERVER_URL.c_str())});
    mySettings->setString(Settings::USER_NAME,
        string{aud_get_str(SETTINGS_SECTION.c_str(), Settings::USER_NAME.c_str())});
    mySettings->setString(Settings::PASSWORD_HASH,
        string{aud_get_str(SETTINGS_SECTION.c_str(), Settings::PASSWORD_HASH.c_str())});
    mySettings->setInt(Settings::LOGGING_VERBOSITY, getVerbosity());
    mySettings->connectChanged([this]() { onSettingsChanged(); });

    myAmpacheBrowser = &myQtApplication->getAmpacheBrowser();
    myAmpacheBrowser->connectPlay([this](vector<string> trackUrls) { onAmpacheBrowserPlay(trackUrls); });
    myAmpacheBrowser->connectCreatePlaylist(
        [this](vector<string> trackUrls) { onAmpacheBrowserCreatePlaylist(trackUrls); });
    myAmpacheBrowser->connectAddToPlaylist(
        [this](vector<string> trackUrls) { onAmpacheBrowserAddToPlaylist(trackUrls); });

    return GeneralPlugin::init();
}



void AmpacheBrowserPlugin::cleanup() {
    myQtApplication->finishRequest([this]() {

        // if the body of onFinished method is defined here destroying of myQtApplication invalidates the
        // captured 'this' pointer which causes segfault when accessing mySettings later on; therefore onFinished
        // cannot be defined as anonymous
        onFinished();
    });
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
    aud_set_bool(SETTINGS_SECTION.c_str(), Settings::USE_DEMO_SERVER.c_str(),
        mySettings->getBool(Settings::USE_DEMO_SERVER));
    aud_set_str(SETTINGS_SECTION.c_str(), Settings::SERVER_URL.c_str(),
        mySettings->getString(Settings::SERVER_URL).c_str());
    aud_set_str(SETTINGS_SECTION.c_str(), Settings::USER_NAME.c_str(),
        mySettings->getString(Settings::USER_NAME).c_str());
    aud_set_str(SETTINGS_SECTION.c_str(), Settings::PASSWORD_HASH.c_str(),
        mySettings->getString(Settings::PASSWORD_HASH).c_str());
}



void AmpacheBrowserPlugin::onFinished() {
    AUDINFO("Finishing.\n");

    myQtApplication = nullptr;
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
