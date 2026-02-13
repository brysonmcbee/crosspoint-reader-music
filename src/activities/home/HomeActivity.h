#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <vector>

#include "../Activity.h"
#include "./MyLibraryActivity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool updateRequired = false;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsUrl = false;
  bool coverRendered = false;      
  bool coverBufferStored = false;  
  uint8_t* coverBuffer = nullptr;  
  std::vector<RecentBook> recentBooks;
  
  // Member functions for navigation
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onMyLibraryOpen;
  const std::function<void()> onRecentsOpen;
  const std::function<void()> onSettingsOpen;
  const std::function<void()> onFileTransferOpen;
  const std::function<void()> onOpdsBrowserOpen;
  // Added member variable for YTMusic navigation
  const std::function<void(const std::string& filename)> onYTMusicOpen; 

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render();
  int getMenuItemCount() const;
  bool storeCoverBuffer();    
  bool restoreCoverBuffer();  
  void freeCoverBuffer();     
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        const std::function<void(const std::string& path)>& onSelectBook,
                        const std::function<void()>& onMyLibraryOpen, const std::function<void()>& onRecentsOpen,
                        const std::function<void()>& onSettingsOpen, const std::function<void()>& onFileTransferOpen,
                        const std::function<void()>& onOpdsBrowserOpen,
                        // Added parameter to constructor
                        const std::function<void(const std::string& filename)>& onYTMusicOpen) 
      : Activity("Home", renderer, mappedInput),
        onSelectBook(onSelectBook),
        onMyLibraryOpen(onMyLibraryOpen),
        onRecentsOpen(onRecentsOpen),
        onSettingsOpen(onSettingsOpen),
        onFileTransferOpen(onFileTransferOpen),
        onOpdsBrowserOpen(onOpdsBrowserOpen),
        // Initialize the member variable
        onYTMusicOpen(onYTMusicOpen) {} 
  void onEnter() override;
  void onExit() override;
  void loop() override;
};