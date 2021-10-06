#pragma once
#include <algorithm>
#include <unordered_map>
#include <bitset>
#include <filesystem>
#include <zlib.h>
#include <cstdint>
#include <string>
#include <iostream>
#include <fstream>
#include <utility>
#include <cassert>

#define CHUNK 16384

static const char DATFILESIGNATURE[4] = {'\xB1', '\x44', '\x41', '\x54'};
static const uint8_t DATFILEVERSION = 0x02;

/**
 * @file
 * Creates a file at the given path, creating the directories up to it if required
 * @param file The file variable that will be opened
 * @param Path The path that the file should be created at
 * @param Force Whether to overwrite the file if it already exists
 * @param Binary Whether to open the file as a binary
 * @return Whether the file was successfully created
 */
bool createFile(std::ofstream& File, const std::string& Path, bool Force = false, bool Binary = false) {
	// Check it doesn't already exist
	if (std::filesystem::exists(Path)) {
		if (Force) {
			remove(Path.c_str());
		}
		else {
			std::cout << "File \"" << Path << "\" exists";
			return false;
		}
	}

	// Create Directories
	std::filesystem::create_directories(Path.substr(0, Path.find_last_of("\\/")));

	if (Binary)	File.open(Path, std::ios::binary | std::ios::out);
	else File.open(Path, std::ios::out);
	return true;
}

struct FileDescriptor {
    bool compressed;
    bool encrypted;
    std::string destDirectory;

    FileDescriptor(bool compressed, bool encrypted, std::string destDirectory) : compressed(compressed),
                                                                                 encrypted(encrypted),
                                                                                 destDirectory(std::move(destDirectory)) {}
};

struct FileFlags {
	bool compressed = false;
	bool encrypted = false;

	/**
	 * Sets up the flags from the filetype byte
	 * @param FlagByte The byte from the file containing the flags
	 */
	void setFlags(uint8_t FlagByte) {
		// Convert to a convenient data type for bit manipulation
		std::bitset<8> bits(FlagByte);

		// Check bits
        compressed = bits.test(7);

        encrypted = bits.test(6);
	}
};

struct DatFileEntry {
	uint8_t fileType = 0;
	FileFlags flags;
	uint32_t crc = 0L;
	int64_t dataSize = 0;
	int64_t dataStart = 0;
	int64_t dataEnd = 0;

	/**
	 * Gets the filetype and flags as a byte
	 * @return a byte containing the filetype and flags ready for writing to a file
	 */
	[[nodiscard]] uint8_t getTypeAndFlags() const {
		uint8_t result = fileType;
		// Set bits
		if (flags.compressed) {
			result |= 0b10000000;
		}
		if (flags.encrypted) {
			result |= 0b01000000;
		}
		return result;
	}
};