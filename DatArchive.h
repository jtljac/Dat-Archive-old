#pragma once
#include <Dat-Archive/DatArchive/DatArchiveCommon.h>

class DatFile {
	std::fstream datFile;
	uint8_t version = 0;
	std::unordered_map<std::string, DatFileEntry> fileTable;
	
public:
	DatFile() {}
	DatFile(std::filesystem::path Path) {
		openFile(Path);
	}
	~DatFile() {
		fileTable.clear();
	}
	/**
	 * Opens the given archive, builds the filetable in the process
	 * @param TheFile The directory of the file to open
	 * @return whether archive was successfully opened
	 */
	bool openFile(std::filesystem::path TheFile) {
		// Open file as a binary, ensure its a file
		datFile.open(TheFile, std::ios::in | std::ios::binary);
		if (!datFile) {
			return false;
		}

		// Read the signiture, check its the right one
		uint32_t signiture;
		datFile.read(reinterpret_cast<char*>(&signiture), 4);
		signiture = _byteswap_ulong(signiture);

		if (signiture != DATFILESIGNITURE) {
			return false;
		}

		// Get the version of the file, check its the right one
		datFile.read(reinterpret_cast<char*>(&version), 1);

		if (version != DATFILEVERSION) {
			return false;
		}

		// Get the table offset
		uint32_t tableOffset;

		datFile.read(reinterpret_cast<char*>(&tableOffset), 4);
		tableOffset = _byteswap_ulong(tableOffset);

		// Go to the table
		datFile.seekg(tableOffset);

		// Build the File Table
		DatFileEntry entry;
		std::string fileName;
		uint8_t buffer;
		while (datFile.read(reinterpret_cast<char*>(&buffer), 1) && !datFile.fail()) {
			/*
			 * Lots of byte swapping here, x86-64 is little endian, would be fine if we used char[] as they don't care for endianess
			 * However we're using uint32_t, which does care about endianess, so we need to reverse the order of the bytes as we load them in
			 */

			char* name = new char[buffer + 1];

			datFile.read(name, buffer);

			// Null Terminate the name
			name[buffer] = '\0';

			// Free up memory, invalidate pointer
			fileName = (std::string) name;
			delete[] name;
			name = nullptr;

			// File desc
			datFile.read(reinterpret_cast<char*>(&buffer), 1);
			entry.flags.setFlags(buffer);
			entry.fileType = buffer & 0b00111111;

			// CRC
			datFile.read(reinterpret_cast<char*>(&entry.crc), 4);
			entry.crc = _byteswap_ulong(entry.crc);

			// Data Size
			datFile.read(reinterpret_cast<char*>(&entry.dataSize), 4);
			entry.dataSize = _byteswap_ulong(entry.dataSize);

			// Set start and end points
			datFile.read(reinterpret_cast<char*>(&entry.dataStart), 4);
			entry.dataStart = _byteswap_ulong(entry.dataStart);
			datFile.read(reinterpret_cast<char*>(&entry.dataEnd), 4);
			entry.dataEnd = _byteswap_ulong(entry.dataEnd);
			fileTable[fileName] = entry;
		}

		datFile.clear();
		return true;
	}

	/**
	 * Decompresses the first given stream into the second given stream
	 * @param In The buffer containing the compressed data
	 * @param Out The buffer for the uncompressed data to end up in
	 * @return The result of the decomression (Z_OK if successful)
	 */
	int decompressToBuffer(char* In, uint32_t InSize, char* Out, uint32_t OutSize) {
		// Vars
		int rc;
		z_stream strm;

		// Setup strm
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		strm.avail_in = 0;
		strm.next_in = Z_NULL;

		// Start inflation
		rc = inflateInit(&strm);
		if (rc != Z_OK)return rc;

		strm.avail_in = InSize;
		if (strm.avail_in == 0) {
			inflateEnd(&strm);
			return Z_DATA_ERROR;
		}

		// Since we're know the decompressed size, and want to keep everything in memory, we can just set the in and out to 2 buffers in memory and inflate all at once
		strm.next_in = reinterpret_cast<unsigned char*>(In);
		strm.avail_out = OutSize;
		strm.next_out = reinterpret_cast<unsigned char*>(Out);
		rc = inflate(&strm, Z_NO_FLUSH);
		assert(rc != Z_STREAM_ERROR);  /* state not clobbered */

		// Check for errors
		switch (rc) {
		case Z_NEED_DICT:
			rc = Z_DATA_ERROR;     /* and fall through */
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			(void)inflateEnd(&strm);
			return rc;
		}

		// Clean up
		inflateEnd(&strm);
		return rc == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
	}

	/**
	 * Gets a file from the archive as a char array
	 * @param File The path to the file in the archive
	 * @param Buf A pointer to a pointer to the buffer where the file data will end up
	 * @return the size of the file
	 */
	unsigned int getFile(std::string File, char** Buf) {
		// Check if the table actually contains the file
		if (!fileTable.count(File)) {
			std::cout << "Attempted to get file: " << File << ", but it doesn't exist" << std::endl;
			return 0;
		}
		
		// Get the table entry for the file, and work out where the data is
		DatFileEntry& entry = fileTable[File];
		uint32_t dataSize = entry.dataEnd - entry.dataStart + 1;

		// Create a buffer big enough for the data
		char* buffer = new char[dataSize];

		// Goto and read the data
		datFile.seekg(entry.dataStart);
		datFile.read(buffer, dataSize);

		// Generate and check the crc to make sure the data is valid
		uint32_t genereatedCRC = crc32(0L, reinterpret_cast<unsigned char*>(buffer), dataSize);
		if (entry.crc != genereatedCRC) {
			std::cout << "File: " << File << " does not match its expected CRC, this usually means the data is corrupt" << std::endl << "Expected: " << std::hex << entry.crc << ", Recieved: " << genereatedCRC << std::endl;
		}

		// Decompress the data if it's compressed
		if (entry.flags.compressed) {
			char* fullBuffer = new char[entry.dataSize];
			decompressToBuffer(buffer, dataSize, fullBuffer, entry.dataSize);
			*Buf = fullBuffer;

			// Clear up the memory used by the compressed buffer and invalidate the pointer
			delete[] buffer;
			buffer = nullptr;
		}
		else {
			*Buf = buffer;
		}

		return entry.dataSize;
	}

	/**
	 * Returns a list of all the files inside the archive file
	 * @return A vector containing all of the files in the archive
	 */
	std::vector<std::string> getFiles() {
		// Create the list to return, set it to the right size so we don't have to keep moving it as we add items
		std::vector<std::string> keys;
		keys.reserve(fileTable.size());

		// Add all the keys
		for (std::unordered_map<std::string, DatFileEntry>::iterator key = fileTable.begin(); key != fileTable.end(); ++key) {
			keys.push_back(key->first);
		}

		return keys;
	}
};

