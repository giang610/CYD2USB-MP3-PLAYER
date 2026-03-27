// ============================================================
//  CYD2USB MP3 Player
//  Board  : ESP32-2432S028 (Cheap Yellow Display 2 USB)
//  Audio  : DAC nội GPIO26 → Loa 8Ω 2W
//  Màn    : ILI9341 2.8" SPI
//  Cảm ứng: XPT2046
//  SD Card: SPI riêng
// ============================================================

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2SNoDAC.h>

// ── Pin SD Card (SPI bus riêng) ──────────────────────────────
#define SD_CS    5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

// ── Pin cảm ứng ──────────────────────────────────────────────
#define TOUCH_CS  33
#define TOUCH_IRQ 36

// ── Màu sắc giao diện ────────────────────────────────────────
#define COLOR_BG       0x1082   // Nền tối
#define COLOR_ACCENT   0x04FF   // Xanh cyan
#define COLOR_BTN      0x2945   // Nút xám đậm
#define COLOR_BTN_ACT  0x0319   // Nút xanh lá khi active
#define COLOR_TEXT     TFT_WHITE
#define COLOR_SUB      0x8410   // Chữ phụ xám

// ── Đối tượng toàn cục ───────────────────────────────────────
TFT_eSPI          tft;
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
SPIClass          sdSPI(HSPI);

AudioGeneratorMP3    *mp3  = nullptr;
AudioFileSourceSD    *file = nullptr;
AudioOutputI2SNoDAC  *out  = nullptr;

// ── Danh sách nhạc ───────────────────────────────────────────
#define MAX_SONGS 100
String playlist[MAX_SONGS];
int    songCount   = 0;
int    currentSong = 0;
bool   isPlaying   = false;
float  volume      = 0.6f;

// ── Biến giao diện ───────────────────────────────────────────
unsigned long lastTouchTime = 0;
unsigned long playStartTime = 0;

// ============================================================
//  Quét SD lấy danh sách MP3
// ============================================================
void scanSD(File dir) {
  while (songCount < MAX_SONGS) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      name.toUpperCase();
      if (name.endsWith(".MP3")) {
        String path = "/" + String(entry.name());
        playlist[songCount++] = path;
      }
    }
    entry.close();
  }
}

// ============================================================
//  Vẽ giao diện chính
// ============================================================
void drawUI() {
  tft.fillScreen(COLOR_BG);

  // ── Header ──
  tft.fillRect(0, 0, 320, 40, COLOR_ACCENT);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 11);
  tft.print("  MP3 PLAYER");

  // ── Tên bài nhạc ──
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("Bai hat:");

  tft.setTextColor(COLOR_ACCENT);
  tft.setTextSize(1);
  String songName = playlist[currentSong];
  songName.replace("/", "");
  songName.replace(".mp3", "");
  songName.replace(".MP3", "");
  if (songName.length() > 36) songName = songName.substring(0, 36) + "..";
  tft.setCursor(10, 62);
  tft.print(songName);

  // ── Số thứ tự bài ──
  tft.setTextColor(COLOR_SUB);
  tft.setTextSize(1);
  tft.setCursor(10, 78);
  tft.print("Track " + String(currentSong + 1) + " / " + String(songCount));

  // ── Thanh phân cách ──
  tft.drawFastHLine(0, 90, 320, COLOR_BTN);

  // ── Nút PREV |<< ──
  tft.fillRoundRect(8, 100, 90, 55, 10, COLOR_BTN);
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(22, 120);
  tft.print("|<<");

  // ── Nút PLAY / PAUSE ──
  uint16_t playColor = isPlaying ? 0xF800 : COLOR_BTN_ACT; // Đỏ=pause, Xanh=play
  tft.fillRoundRect(110, 100, 100, 55, 10, playColor);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  if (isPlaying) {
    tft.setCursor(132, 120);
    tft.print("||");
  } else {
    tft.setCursor(130, 120);
    tft.print("|>");
  }

  // ── Nút NEXT >>| ──
  tft.fillRoundRect(222, 100, 90, 55, 10, COLOR_BTN);
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(236, 120);
  tft.print(">>|");

  // ── Thanh âm lượng ──
  tft.setTextColor(COLOR_SUB);
  tft.setTextSize(1);
  tft.setCursor(10, 168);
  tft.print("Am luong:");

  // Nút vol -
  tft.fillRoundRect(8, 180, 55, 40, 8, COLOR_BTN);
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(3);
  tft.setCursor(22, 188);
  tft.print("-");

  // Thanh vol
  int volBar = (int)(volume * 190);
  tft.drawRoundRect(70, 190, 190, 20, 5, COLOR_SUB);
  tft.fillRoundRect(70, 190, volBar, 20, 5, COLOR_ACCENT);

  // Nút vol +
  tft.fillRoundRect(268, 180, 44, 40, 8, COLOR_BTN);
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(3);
  tft.setCursor(278, 188);
  tft.print("+");

  // ── Trạng thái ──
  tft.setTextColor(COLOR_SUB);
  tft.setTextSize(1);
  tft.setCursor(10, 228);
  if (isPlaying) {
    tft.print(">> Dang phat...");
  } else {
    tft.print("|| Tam dung");
  }
}

// ============================================================
//  Hiển thị màn hình khởi động
// ============================================================
void drawBoot(String msg) {
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(1);
  tft.setCursor(10, tft.getCursorY());
  tft.println(msg);
}

// ============================================================
//  Phát bài nhạc
// ============================================================
void playSong(int index) {
  // Dừng bài cũ
  if (mp3 && mp3->isRunning()) {
    mp3->stop();
  }
  if (mp3)  { delete mp3;  mp3  = nullptr; }
  if (file) { delete file; file = nullptr; }

  String path = playlist[index];
  Serial.println("Playing: " + path);

  file = new AudioFileSourceSD(path.c_str());
  mp3  = new AudioGeneratorMP3();
  mp3->begin(file, out);

  isPlaying     = true;
  playStartTime = millis();
  drawUI();
}

void stopSong() {
  if (mp3 && mp3->isRunning()) mp3->stop();
  isPlaying = false;
  drawUI();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // ── Màn hình ──
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);   // BẮT BUỘC với CYD2USB!
  tft.fillScreen(COLOR_BG);

  // Boot screen
  tft.fillRect(0, 0, 320, 40, COLOR_ACCENT);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(60, 12);
  tft.print("MP3 PLAYER");
  tft.setCursor(0, 50);

  drawBoot("Khoi dong...");

  // ── Cảm ứng ──
  ts.begin();
  ts.setRotation(0);
  drawBoot("Touch OK");

  // ── SD Card ──
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) {
    tft.setTextColor(TFT_RED);
    tft.println("SD THAT BAI! Kiem tra lai.");
    while (1) delay(1000);
  }
  drawBoot("SD Card OK");

  // ── Quét nhạc ──
  File root = SD.open("/");
  scanSD(root);
  root.close();

  if (songCount == 0) {
    tft.setTextColor(TFT_RED);
    tft.println("Khong co file MP3 trong SD!");
    while (1) delay(1000);
  }
  drawBoot("Tim thay " + String(songCount) + " bai hat");
  delay(800);

  // ── Audio DAC nội GPIO26 ──
  out = new AudioOutputI2SNoDAC();
  out->SetOutputModeMono(true);
  out->SetGain(volume);
  drawBoot("Audio DAC OK");
  delay(400);

  // ── Vẽ UI và phát bài đầu ──
  playSong(0);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Tiếp tục phát nhạc
  if (mp3 && mp3->isRunning()) {
    if (!mp3->loop()) {
      // Hết bài → tự chuyển bài tiếp theo
      currentSong = (currentSong + 1) % songCount;
      playSong(currentSong);
      return;
    }
  }

  // Xử lý cảm ứng (debounce 300ms)
  if (millis() - lastTouchTime < 300) return;

  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();

    // Chuyển đổi tọa độ cảm ứng → tọa độ màn hình
    int x = map(p.x, 200, 3800, 0, 320);
    int y = map(p.y, 200, 3800, 0, 240);

    lastTouchTime = millis();
    Serial.printf("Touch: x=%d y=%d\n", x, y);

    // ── Nút PREV (x:8-98, y:100-155) ──
    if (x > 8 && x < 98 && y > 100 && y < 155) {
      currentSong = (currentSong - 1 + songCount) % songCount;
      playSong(currentSong);
    }

    // ── Nút PLAY/PAUSE (x:110-210, y:100-155) ──
    else if (x > 110 && x < 210 && y > 100 && y < 155) {
      if (isPlaying) {
        stopSong();
      } else {
        playSong(currentSong);
      }
    }

    // ── Nút NEXT (x:222-312, y:100-155) ──
    else if (x > 222 && x < 312 && y > 100 && y < 155) {
      currentSong = (currentSong + 1) % songCount;
      playSong(currentSong);
    }

    // ── Vol DOWN (x:8-63, y:180-220) ──
    else if (x > 8 && x < 63 && y > 180 && y < 220) {
      volume = max(0.0f, volume - 0.1f);
      out->SetGain(volume);
      drawUI();
    }

    // ── Vol UP (x:268-312, y:180-220) ──
    else if (x > 268 && x < 312 && y > 180 && y < 220) {
      volume = min(1.0f, volume + 0.1f);
      out->SetGain(volume);
      drawUI();
    }
  }
}
