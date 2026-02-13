#pragma once

#include "ActivityWithSubactivity.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <functional>

enum class YTMusicState {
    WIFI_CONNECTING,
    POLLING_STATUS,
    ERROR
};

class YTMusicActivity : public ActivityWithSubactivity {
public:
    YTMusicActivity(GfxRenderer& r, MappedInputManager& i, std::string filename, std::function<void()> onExitCB);

    void onEnter() override;
    void onExit() override;
    void loop() override;

private:
    void render() const;
    void pollServerStatus();
    void sendControl(const char* action);
    bool downloadAlbumArt(); // New function for image sync
    void connectWifiSilently();
    
    static void taskTrampoline(void* param);
    void displayTaskLoop();

    // Navigation and Identity
    std::string startFilename;
    std::function<void()> onExitCallback;
    
    // Server and State Management
    const std::string serverIP = "192.168.68.57"; // Updated to your confirmed IP
    YTMusicState state = YTMusicState::WIFI_CONNECTING;
    
    // Refresh & Task Management
    bool firstLoadDone = false;
    bool updateRequired = true;
    volatile bool isExiting = false; 
    unsigned long lastPollTime = 0;

    // Concurrency
    TaskHandle_t displayTaskHandle = nullptr;
    SemaphoreHandle_t renderingMutex = nullptr;

    // Metadata Storage
    struct {
        String title = "Connecting...";
        String artist = "Please wait";
        String imgHash = "";
    } currentTrack;

    const char* TEMP_ART_PATH = "/temp_art.bmp";
};