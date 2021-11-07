#pragma once
#include <DatArchive/DatArchiveCommon.h>

#include <utility>

class DatFile {
	std::fstream datFile;
	uint8_t version = 0;
	std::unordered_map<std::string, DatFileEntry> fileTable;
	
public:
	DatFile() = default;

	explicit DatFile(const std::filesystem::path& Path) {
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
	bool openFile(const std::filesystem::path& TheFile) {
		// Open file as a binary, ensure its a file
		datFile.open(TheFile, std::ios::in | std::ios::binary);
		if (!datFile) {
			return false;
		}

		// Read the signature, check its the right one
		char signature[4];
		datFile.read(signature, 4);

		if (!strcmp(signature, DATFILESIGNATURE)) {
			return false;
		}

		// Get the version of the file, check it's the right one
		datFile.read(reinterpret_cast<char*>(&version), 1);

		if (version != DATFILEVERSION) {
			return false;
		}

		// Get the table offset
		int64_t tableOffset;

		datFile.read(reinterpret_cast<char*>(&tableOffset), 8);

		// Go to the table
		datFile.seekg(tableOffset);

		// Build the File Table
		DatFileEntry entry;
		std::string fileName;
		uint8_t buffer;
		while (datFile.read(reinterpret_cast<char*>(&buffer), 1) && !datFile.fail()) {
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

			// Data Size
			datFile.read(reinterpret_cast<char*>(&entry.dataSize), 8);

			// Set start and end points
			datFile.read(reinterpret_cast<char*>(&entry.dataStart), 8);
			datFile.read(reinterpret_cast<char*>(&entry.dataEnd), 8);

			fileTable[fileName] = entry;
		}

		datFile.clear();
		return true;
	}

	/**
	 * Decompresses the first given stream into the second given stream
	 * @param In The buffer containing the compressed data
	 * @param Out The buffer for the uncompressed data to end up in
	 * @return The result of the decompression (Z_OK if successful)
	 */
	static int decompressToBuffer(char* In, uint32_t InSize, char* Out, uint32_t OutSize) {
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

		// Since we know the decompressed size, and want to keep everything in memory, we can just set the in and out to 2 buffers in memory and inflate all at once
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
     * Gets a file from the archive as a vector of chars
     * @param filePath The part to the file in the archive
     * @return A vector containing all the bytes of the file in the archive
     */
    std::vector<char> getFile(const std::string& filePath) {
        if (!fileTable.count(filePath)) {
            std::cout << "Attempted to get file: " << filePath << ", but it doesn't exist" << std::endl;
            return {};
        }
        DatFileEntry& entry = fileTable[filePath];

        // Create a buffer big enough for the data
        std::vector<char> buffer(entry.size());

        if (getFile(filePath, buffer.data())) return buffer;
        else return {};
    }

	/**
	 * Gets a file from the archive as a char array
	 * Warning, this function assumes that the buffer is already big enough to store the file
	 * @param File The path to the file in the archive
	 * @param buffer A pointer to a pointer to the buffer where the file data will end up (assumed to be the correct size already)
	 * @return If the buffer was successfully filled
	 */
	bool getFile(const std::string& File, char* buffer) {
		// Check if the table actually contains the file
		if (!fileTable.count(File)) {
			std::cout << "Attempted to get file: " << File << ", but it doesn't exist" << std::endl;
            return false;
		}
		
		// Get the table entry for the file, and work out where the data is
		DatFileEntry& entry = fileTable[File];
		int64_t dataSize = entry.dataEnd - entry.dataStart + 1;

        char* destBuffer;

        if (entry.flags.compressed) {
            destBuffer = new char[dataSize];
        } else {
            destBuffer = buffer;
        }

		// Goto and read the data
		datFile.seekg(entry.dataStart);
		if (!datFile.read(destBuffer, dataSize).good()) {
            if (entry.flags.compressed) delete[] destBuffer;
            return false;
        }

		// Generate and check the crc to make sure the data is valid
		uint32_t generatedCrc = crc32(0L, reinterpret_cast<unsigned char*>(destBuffer), dataSize);
		if (entry.crc != generatedCrc) {
			std::cout << "File: " << File << " does not match its expected CRC, this usually means the data is corrupt" << std::endl << "Expected: " << std::hex << entry.crc << ", Received: " << generatedCrc << std::dec << std::endl;
        }

		// Decompress the data if it's compressed
		if (entry.flags.compressed) {
			decompressToBuffer(destBuffer, dataSize, buffer, entry.dataSize);
            delete[] destBuffer;
		}

        return true;
	}

    /**
     * Gets the header for the file at the address
     * @param filePath
     * @return
     */
    [[nodiscard]] const DatFileEntry& getFileHeader(const std::string& filePath) {
        return fileTable[filePath];
    }

    /**
     * Checks if the archive file contains a file at the given path
     * @param filePath The path to the file to check in the archive
     * @return If the archive file contains a file at the filePath
     */
    [[nodiscard]] bool contains(const std::string& filePath) const {
        return fileTable.count(filePath) != 0;
    }

    /**
     * Returns the amount of fules stored inside the archive file
     * @return The amount of files stored inside the archive file
     */
    [[nodiscard]] size_t size() const {
        return fileTable.size();
    }

	/**
	 * Returns a list of all the files inside the archive file
	 * @return A vector containing all of the files in the archive
	 */
	std::vector<std::string> getListOfFiles() {
		// Create the list to return, set it to the right size so we don't have to keep moving it as we add items
		std::vector<std::string> keys(fileTable.size());

		// Add all the keys
		for (auto& key : fileTable) {
			keys.push_back(key.first);
		}

		return keys;
	}
};

