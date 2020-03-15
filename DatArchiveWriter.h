#pragma once

#include "DatArchive/DatFileCommon.h"

#include <json.hpp>
using json = nlohmann::json;

class DatFileWriter {
	std::ofstream* archiveFile = nullptr;
	std::unordered_map<std::string, DatFileEntry> table;
	/**
	 * Creates the initial archive file
	 * @param FilePath The path for the file to end up in
	 * @param force Whether to overrite the path at FilePath if it exists
	 */
	DatFileWriter(std::string FilePath, bool Force = true) {
		// Create the File
		archiveFile = new std::ofstream;
		createFile(*archiveFile, FilePath, Force, true);

		// Write Header
		uint32_t bigBuffer = DATFILESIGNITURE;
		bigBuffer = _byteswap_ulong(bigBuffer);
		archiveFile->write(reinterpret_cast<char*>(&bigBuffer), 4);
		archiveFile->write(reinterpret_cast<const char*>(&DATFILEVERSION), 1);

		// Reserve header
		char empty[4] = { 0x00, 0x00, 0x00, 0x00 };
		archiveFile->write(empty, 4);

		// Write to file
		archiveFile->flush();
	}

	/**
	 * Compresses the data from one file stream and deposits it in the next one
	 * @param Source A pointer to the stream to compress
	 * @param Dest A pointer to the stream to put the compressed data into
	 * @param CRC A reference to the a uint32_t to put the resulting CRC32 into
	 * @param Level The ZLib compression level
	 * @result The result of the compression (Success is Z_OK)
	 */
	int compressFileToStream(std::ifstream* Source, std::ofstream* Dest, uint32_t& CRC, int Level) {
		// States
		int rc, flushState;

		// Amount left
		unsigned have;

		// Stream
		z_stream strm;

		// CRC32
		CRC = crc32(0L, Z_NULL, 0);

		// Buffers
		unsigned char* in = new unsigned char[CHUNK];
		unsigned char* out = new unsigned char[CHUNK];

		// Difference
		uint32_t diff;

		// Setup stream
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;

		// Start Deflation
		rc = deflateInit(&strm, Level);
		if (rc != Z_OK) return rc;

		do {
			// Read in some data from the file
			Source->read(reinterpret_cast<char*>(in), CHUNK);
			strm.avail_in = Source->gcount();

			// If there was no data read then somethings gone wrong, stop deflating, free memory, and return an error
			if (!Source) {
				deflateEnd(&strm);
				delete[](in);
				in = nullptr;

				delete[](out);
				out = nullptr;
				return Z_ERRNO;
			}

			flushState = Source->eof() ? Z_FINISH : Z_NO_FLUSH;
			strm.next_in = in;

			do {
				strm.avail_out = CHUNK;
				strm.next_out = out;

				// Deflate the data
				rc = deflate(&strm, flushState);
				assert(rc != Z_STREAM_ERROR);

				have = CHUNK - strm.avail_out;

				// Generate the CRC for that chunk
				CRC = crc32(CRC, out, have);

				// Write the data to th efile, work out the amount of data written by taking the point in the file after writing and subtracting the point before
				diff = Dest->tellp();
				Dest->write(reinterpret_cast<char*>(out), have);
				diff = ((uint32_t)Dest->tellp()) - diff;

				// If the amount of data written is less than the chunk size after decompression then somethings gone wrong, stop deflating, free memory, and return an error
				if (diff != have || !Dest) {
					deflateEnd(&strm);
					delete[](in);
					in = nullptr;

					delete[](out);
					out = nullptr;
					return Z_ERRNO;
				}
			} while (strm.avail_out == 0);
			assert(strm.avail_in == 0);
		} while (flushState != Z_FINISH);
		assert(rc == Z_STREAM_END);

		// Successfully deflated, clean up
		deflateEnd(&strm);

		delete[](in);
		in = nullptr;

		delete[](out);
		out = nullptr;
		return Z_OK;
	}

	/**
	 * Copies one stream into the next
	 * @param Source A pointer to the stream to copy
	 * @param Dest A pointer to the stream to put the data into
	 * @param CRC A reference to the a uint32_t to put the resulting CRC32 into
	 */
	void fileToStream(std::ifstream* Source, std::ofstream* Dest, uint32_t& CRC) {
		// Amount left
		unsigned have;

		// CRC32
		CRC = crc32(0L, Z_NULL, 0);

		// Buffers
		unsigned char* buffer = new unsigned char[CHUNK];

		while (!Source->eof()) {
			/*
			 * We'd probably have enough memory for this anyway, but we'll chunk it anyway, just incase.
			 */
			 // Fill buffer
			Source->read(reinterpret_cast<char*>(buffer), CHUNK);
			have = Source->gcount();

			// Generate CRC
			CRC = crc32(CRC, buffer, have);

			// Write to file
			Dest->write(reinterpret_cast<char*>(buffer), have);
		}
	}

	/**
	 * Writes the data of a given file into the archive, treating it how the descriptor tells us to
	 * @param File The path to the file on the disk
	 * @param Descriptor A json object describing the file
	 * @return Whether the file write was a success
	 */
	bool writeFile(std::string File, json& Descriptor) {
		DatFileEntry entry;

		// Work out start
		entry.dataStart = ((uint32_t)archiveFile->tellp());

		// Open the file, return false if the file wasn't opened
		std::ifstream theFile(File, std::ios::binary | std::ios::in);
		if (!theFile) {
			std::cout << "Could not open the target file" << std::endl;
			return false;
		}

		// Write data
		if (Descriptor["Compress"]) {
			// Set the compressed flag
			entry.flags.compressed = true;

			// Return false if the file was not successfully compressed
			if (compressFileToStream(&theFile, archiveFile, entry.crc, Z_DEFAULT_COMPRESSION) != Z_OK) {
				std::cout << "Failed to compress the file" << std::endl;
				return false;
			}
		}
		else {
			fileToStream(&theFile, archiveFile, entry.crc);
		}

		// Get the size in bytes of the file we just added to the archive
		theFile.clear();
		theFile.seekg(0, std::ios::end);
		entry.dataSize = ((uint32_t)theFile.tellg());

		// We're done with the file, close it
		theFile.close();

		// Work out end
		entry.dataEnd = ((uint32_t)archiveFile->tellp()) - 1;

		// Add entry to table
		table[Descriptor["DestDirectory"]] = entry;
		archiveFile->flush();

		// Return success
		return true;
	}

	/**
	 * Finishes the archive file, writing the filetable to the end
	 */
	void finish() {
		// Work out table offset
		uint32_t tableOffset = _byteswap_ulong(archiveFile->tellp());

		// Create buffers
		uint8_t buffer = 0;
		uint32_t bigBuffer = 0;

		// Add all table entries to the table
		for (std::unordered_map<std::string, DatFileEntry>::iterator it = table.begin(); it != table.end(); ++it) {
			// Name Length
			buffer = strlen(it->first.c_str());
			archiveFile->write(reinterpret_cast<char*>(&buffer), 1);

			// Name
			archiveFile->write(it->first.c_str(), buffer);

			// Desc
			buffer = it->second.getTypeAndFlags();
			archiveFile->write(reinterpret_cast<char*>(&buffer), 1);

			// CRC
			bigBuffer = _byteswap_ulong(it->second.crc);
			archiveFile->write(reinterpret_cast<char*>(&bigBuffer), 4);

			// Data Size
			bigBuffer = _byteswap_ulong(it->second.dataSize);
			archiveFile->write(reinterpret_cast<char*>(&bigBuffer), 4);

			// Data Start
			bigBuffer = _byteswap_ulong(it->second.dataStart);
			archiveFile->write(reinterpret_cast<char*>(&bigBuffer), 4);

			// Data End
			bigBuffer = _byteswap_ulong(it->second.dataEnd);
			archiveFile->write(reinterpret_cast<char*>(&bigBuffer), 4);
		}

		// Go to the table offset
		archiveFile->seekp(5);

		// Write in the new table offset
		archiveFile->write(reinterpret_cast<char*>(&tableOffset), 4);

		// Write and close the file
		archiveFile->flush();
		archiveFile->close();
		delete(archiveFile);
		archiveFile = nullptr;
	}
};