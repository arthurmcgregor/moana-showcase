#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "mbedtls/base64.h"

// Sketch allows reading, writing, and deleting files from LittleFS via Serial

// Forward declarations
void listDir(fs::FS& fs, const char* dirname, uint8_t levels);
void readFile(const String& path);
void dumpFileHex(const String& path);
void deleteFile(const String& path);
void downloadFile(const String& path);
void downloadAllFiles();

void setup() {
  Serial.begin(115200);
  delay(2000);  // Give time to open serial monitor

  Serial.println("\n\n=== LittleFS Recovery Tool ===");
  Serial.println("Mounting LittleFS...");

  if (!LittleFS.begin(false)) {
    Serial.println("FATAL: Cannot mount filesystem");
    return;
  }

  Serial.println("LittleFS mounted successfully!");
  Serial.println("\n--- Filesystem Info ---");
  Serial.printf("Total bytes: %d\n", LittleFS.totalBytes());
  Serial.printf("Used bytes: %d\n", LittleFS.usedBytes());
  Serial.printf("Free bytes: %d\n", LittleFS.totalBytes() - LittleFS.usedBytes());

  Serial.println("\n--- Listing Files ---");
  listDir(LittleFS, "/", 3);

  Serial.println("\n=== Commands ===");
  Serial.println("Type: READ <filename>     - Read and display file");
  Serial.println("Type: DUMP <filename>     - Dump file in hex");
  Serial.println("Type: LIST                - List all files");
  Serial.println("Type: DELETE <filename>   - Delete a file");
  Serial.println("Type: FORMAT              - Format filesystem (DANGER!)");
  Serial.println("Type: DOWNLOAD <filename> - Download file (use tools/lfs_tool.py)");
  Serial.println("Type: DOWNLOAD_ALL        - Download all files (use tools/lfs_tool.py)");
  Serial.println("Ready for commands...");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("READ ")) {
      String filename = cmd.substring(5);
      filename.trim();
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      readFile(filename);

    } else if (cmd.startsWith("DUMP ")) {
      String filename = cmd.substring(5);
      filename.trim();
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      dumpFileHex(filename);

    } else if (cmd.startsWith("DOWNLOAD ")) {
      String filename = cmd.substring(9);
      filename.trim();
      if (!filename.startsWith("/")) filename = "/" + filename;
      downloadFile(filename);

    } else if (cmd == "DOWNLOAD_ALL") {
      downloadAllFiles();

    } else if (cmd == "LIST") {
      Serial.println("\n--- Listing Files ---");
      listDir(LittleFS, "/", 3);
      Serial.println("LIST_END");

    } else if (cmd.startsWith("DELETE ")) {
      String filename = cmd.substring(7);
      filename.trim();
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      deleteFile(filename);

    } else if (cmd == "FORMAT") {
      Serial.println("WARNING: This will erase all files!");
      Serial.println("Type YES to confirm:");
      while (!Serial.available()) {
        delay(100);
      }
      String confirm = Serial.readStringUntil('\n');
      confirm.trim();
      if (confirm == "YES") {
        Serial.println("Formatting...");
        LittleFS.format();
        Serial.println("Format complete. Rebooting...");
        delay(1000);
        ESP.restart();
      } else {
        Serial.println("Format cancelled");
      }

    } else {
      Serial.println("Unknown command: " + cmd);
    }

    Serial.println("\nReady for commands...");
  }
}

void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("ERROR: Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("ERROR: Not a directory");
    return;
  }

  File file = root.openNextFile();
  int fileCount = 0;
  while (file) {
    fileCount++;
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels > 1) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    file = root.openNextFile();
  }

  if (fileCount == 0) {
    Serial.println("  (empty)");
  }
  Serial.printf("Total files: %d\n", fileCount);
}

void readFile(const String& path) {
  Serial.printf("\n=== Reading: %s ===\n", path.c_str());

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("ERROR: Failed to open file");
    return;
  }

  if (file.isDirectory()) {
    Serial.println("ERROR: This is a directory");
    file.close();
    return;
  }

  Serial.printf("File size: %d bytes\n", file.size());
  Serial.println("--- Content Start ---");

  while (file.available()) {
    Serial.write(file.read());
  }

  Serial.println("\n--- Content End ---");
  file.close();
}

void dumpFileHex(const String& path) {
  Serial.printf("\n=== Hex Dump: %s ===\n", path.c_str());

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("ERROR: Failed to open file");
    return;
  }

  if (file.isDirectory()) {
    Serial.println("ERROR: This is a directory");
    file.close();
    return;
  }

  Serial.printf("File size: %d bytes\n", file.size());
  Serial.println("--- Hex Dump Start ---");

  int addr = 0;
  while (file.available()) {
    Serial.printf("%08X: ", addr);

    // Read up to 16 bytes
    uint8_t buf[16];
    int bytesRead = file.read(buf, 16);

    // Print hex
    for (int i = 0; i < bytesRead; i++) {
      Serial.printf("%02X ", buf[i]);
    }

    // Padding
    for (int i = bytesRead; i < 16; i++) {
      Serial.print("   ");
    }

    Serial.print(" | ");

    // Print ASCII
    for (int i = 0; i < bytesRead; i++) {
      char c = buf[i];
      if (c >= 32 && c <= 126) {
        Serial.write(c);
      } else {
        Serial.write('.');
      }
    }

    Serial.println();
    addr += bytesRead;
  }

  Serial.println("--- Hex Dump End ---");
  file.close();
}

void deleteFile(const String& path) {
  Serial.printf("Deleting: %s\n", path.c_str());

  if (LittleFS.remove(path)) {
    Serial.println("File deleted successfully");
  } else {
    Serial.println("ERROR: Failed to delete file");
  }
}

void downloadAllFiles() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("ERROR: Cannot open root directory");
    return;
  }

  // Collect all file paths first
  std::vector<String> paths;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      paths.push_back(String(f.path()));
    }
    f = root.openNextFile();
  }
  root.close();

  Serial.printf("DOWNLOAD_ALL_START:%u\n", (unsigned int)paths.size());
  for (const String& path : paths) {
    downloadFile(path);
  }
  Serial.println("DOWNLOAD_ALL_END");
}

void downloadFile(const String& path) {
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    Serial.printf("ERROR: Cannot open file: %s\n", path.c_str());
    if (file) file.close();
    return;
  }

  size_t fileSize = file.size();
  Serial.printf("DOWNLOAD_START:%s:%u\n", path.c_str(), (unsigned int)fileSize);

  uint8_t buf[48];
  unsigned char encoded[65];  // base64: 48 bytes → 64 chars + null
  size_t encodedLen;

  while (file.available()) {
    int bytesRead = file.read(buf, sizeof(buf));
    if (bytesRead <= 0) break;
    mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen, buf, bytesRead);
    encoded[encodedLen] = '\0';
    Serial.println((char*)encoded);
  }

  file.close();
  Serial.println("DOWNLOAD_END");
}
