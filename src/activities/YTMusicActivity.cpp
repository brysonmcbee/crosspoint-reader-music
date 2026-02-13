#include "YTMusicActivity.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include <HalStorage.h> // Ensure you can write to SD

YTMusicActivity::YTMusicActivity(GfxRenderer& r, MappedInputManager& i, std::string filename, std::function<void()> onExitCB) 
    : ActivityWithSubactivity("YTMusic", r, i), 
      startFilename(filename), 
      onExitCallback(onExitCB) {}

void YTMusicActivity::taskTrampoline(void* param) {
    auto* self = static_cast<YTMusicActivity*>(param);
    self->displayTaskLoop();
}

void YTMusicActivity::onEnter() {
    ActivityWithSubactivity::onEnter();
    isExiting = false;
    firstLoadDone = false;

    if (!renderingMutex) {
        renderingMutex = xSemaphoreCreateMutex();
    }

    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 400, "Initializing Music Bridge...", true);
    renderer.displayBuffer(HalDisplay::RefreshMode::HALF_REFRESH);

    connectWifiSilently();
    xTaskCreate(&YTMusicActivity::taskTrampoline, "YTMusicTask", 8192, this, 1, &displayTaskHandle);
}

void YTMusicActivity::displayTaskLoop() {
    while (!isExiting) {
        if (updateRequired) {
            updateRequired = false;
            if (xSemaphoreTake(renderingMutex, portMAX_DELAY)) {
                render();
                
                // Handle the refresh mode here to avoid const issues in render()
                if (!firstLoadDone) {
                    renderer.displayBuffer(HalDisplay::RefreshMode::FULL_REFRESH);
                    firstLoadDone = true;
                } else {
                    renderer.displayBuffer(HalDisplay::RefreshMode::HALF_REFRESH);
                }
                xSemaphoreGive(renderingMutex);
            }
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void YTMusicActivity::sendControl(const char* action) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.setTimeout(1500); 
    String url = "http://" + String(serverIP.c_str()) + ":5000/control/" + action;
    http.begin(url);
    http.GET();
    http.end();
}

void YTMusicActivity::pollServerStatus() {
    HTTPClient http;
    http.setTimeout(2500); 
    http.begin("http://" + String(serverIP.c_str()) + ":5000/status");
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        // Use a fixed size document for stability
        StaticJsonDocument<1024> doc; 
        DeserializationError error = deserializeJson(doc, http.getString());

        if (!error) {
            String newHash = doc["img_hash"] | "initial";
            
            if (newHash != currentTrack.imgHash && newHash != "initial") {
                currentTrack.title = doc["title"] | "Unknown";
                currentTrack.artist = doc["artist"] | "Unknown";
                currentTrack.imgHash = newHash;

                if (downloadAlbumArt()) {
                    updateRequired = true;
                }
            }
        }
    }
    http.end();
}

bool YTMusicActivity::downloadAlbumArt() {
    HTTPClient http;
    http.begin("http://" + String(serverIP.c_str()) + ":5000/image.bmp");
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        // Open file on SD card for writing
        FsFile file = Storage.open(TEMP_ART_PATH, O_WRITE | O_CREAT | O_TRUNC);
        if (file) {
            http.writeToStream(&file);
            file.close();
            Serial.println("[YTMusic] Album art saved to SD.");
            return true;
        }
    }
    http.end();
    return false;
}

void YTMusicActivity::render() const {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 30, "Now Playing", true, EpdFontFamily::BOLD);
    renderer.drawLine(20, 55, 460, 55, true);

    renderer.drawCenteredText(UI_12_FONT_ID, 100, currentTrack.title.c_str(), true);
    renderer.drawCenteredText(UI_10_FONT_ID, 140, currentTrack.artist.c_str());

    FsFile file;
    // Note: ensure Storage.openFileForRead matches your HAL signature
    if (Storage.openFileForRead("YT", TEMP_ART_PATH, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderer.drawBitmap(bitmap, 90, 200, 300, 300, 0.0f, 0.0f);
        }
        file.close();
    } else {
        renderer.drawRect(90, 200, 300, 300, true);
    }

    const auto labels = mappedInput.mapLabels("Exit", "Prev", "Pause", "Next");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ... Rest of the functions (loop, onExit, connectWifiSilently) remain the same ...


void YTMusicActivity::connectWifiSilently() {
    if (WiFi.status() == WL_CONNECTED) return;

    WIFI_STORE.loadFromFile(); 
    const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
    if (lastSsid.empty()) {
        Serial.println("[YTMusic] !! No SSID found in WIFI_STORE");
        return;
    }

    const WifiCredential* cred = WIFI_STORE.findCredential(lastSsid);
    if (!cred) return;

    Serial.printf("[%lu] [YTMusic] Connecting to SSID: %s\n", millis(), lastSsid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(500 / portTICK_PERIOD_MS); 
        attempts++;
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[%lu] [YTMusic] WiFi Connected! IP: %s\n", millis(), WiFi.localIP().toString().c_str());
        state = YTMusicState::POLLING_STATUS;
    } else {
        Serial.println("\n[YTMusic] !! WiFi Connection Failed");
    }
}

void YTMusicActivity::loop() {
    if (this->subActivity) { 
        this->subActivity->loop(); 
        return; 
    }

    if (WiFi.status() != WL_CONNECTED) {
        if (state != YTMusicState::WIFI_CONNECTING) {
            Serial.println("[YTMusic] WiFi lost. Attempting reconnect...");
            state = YTMusicState::WIFI_CONNECTING;
            currentTrack.title = "Offline - Reconnecting";
            updateRequired = true;
            connectWifiSilently();
        }
        return; 
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        Serial.println("[YTMusic] Input: Next Track");
        sendControl("next");
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        Serial.println("[YTMusic] Input: Previous Track");
        sendControl("prev");
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        Serial.println("[YTMusic] Input: Play/Pause Toggle");
        sendControl("pause");
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        Serial.println("[YTMusic] Input: Back (Exiting)");
        if (onExitCallback) onExitCallback();
    }

    if (state == YTMusicState::POLLING_STATUS && (millis() - lastPollTime > 5000)) {
        if (xSemaphoreTake(renderingMutex, 0) == pdTRUE) {
            pollServerStatus();
            xSemaphoreGive(renderingMutex);
            lastPollTime = millis();
        }
    }
}
void YTMusicActivity::onExit() {
    Serial.println("[YTMusic] onExit called. Shutting down activity...");
    isExiting = true; 
    state = YTMusicState::ERROR;

    if (renderingMutex && xSemaphoreTake(renderingMutex, portMAX_DELAY)) {
        renderer.clearScreen();
        renderer.displayBuffer(HalDisplay::RefreshMode::FULL_REFRESH);
        xSemaphoreGive(renderingMutex);
    }
    
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    Serial.println("[YTMusic] Cleanup complete.");
}