#pragma once

#include <DatArchive/DatArchiveCommon.h>

class DatFileWriter {
	std::ofstream* archiveFile = nullptr;
	std::unordered_map<std::string, DatFileEntry> table;

public:
	/**
	 * Creates the initial archive file
	 * @param FilePath The path for the file to end up in
	 * @param force Whether to overrite the path at FilePath if it exists
	 */
	DatFileWriter(const std::string& FilePath, bool Force = true) {
		// Create the File
		archiveFile = new std::ofstream;
		createFile(*archiveFile, FilePath, Force, true);

		// Write Header
		archiveFile->write(DATFILESIGNATURE, 4);
		archiveFile->write(reinterpret_cast<const char*>(&DATFILEVERSION), 1);

		// Reserve header
		char empty[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		archiveFile->write(empty, 8);

		// Write to file
		archiveFile->flush();
	}

private:
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

				// Write the data to the file, work out the amount of data written by taking the point in the file after writing and subtracting the point before
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
			 * We'd probably have enough memory for this anyway, but we'll chunk it anyway, just in case.
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

public:
	/**
	 * Writes the data of a given file into the archive, treating it how the descriptor tells us to
	 * @param File The path to the file on the disk
	 * @param Descriptor A json object describing the file
	 * @return Whether the file write was a success
	 */
	bool writeFile(const std::string& File, const FileDescriptor& Descriptor) {
		DatFileEntry entry;

		// Work out start
		entry.dataStart = (archiveFile->tellp());

		// Open the file, return false if the file wasn't opened
		std::ifstream theFile(File, std::ios::binary | std::ios::in);
		if (!theFile) {
			std::cout << "Could not open the target file" << std::endl;
			return false;
		}

		// Write data
		if (Descriptor.compressed) {
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
		entry.dataSize = (theFile.tellg());

		// We're done with the file, close it
		theFile.close();

		// Work out end
		entry.dataEnd = ((int64_t) archiveFile->tellp()) - 1;

		// Add entry to table
		table[Descriptor.destDirectory] = entry;
		archiveFile->flush();

		// Return success
		return true;
	}

	/**
	 * Finishes the archive file, writing the filetable to the end
	 */
	void finish() {
		// Work out table offset
		int64_t tableOffset = archiveFile->tellp();


        uint8_t buffer;
		// Add all table entries to the table
		for (auto & it : table) {
			// Name Length
            buffer = strlen(it.first.c_str());
			archiveFile->write(reinterpret_cast<char*>(&buffer), 1);

			// Name
			archiveFile->write(it.first.c_str(), buffer);

			// Desc
			buffer = it.second.getTypeAndFlags();
			archiveFile->write(reinterpret_cast<char*>(&buffer), 1);

			// CRC
			archiveFile->write(reinterpret_cast<char*>(&it.second.crc), 4);

			// Data Size
			archiveFile->write(reinterpret_cast<char*>(&it.second.dataSize), 8);

			// Data Start
			archiveFile->write(reinterpret_cast<char*>(&it.second.dataStart), 8);

			// Data End
			archiveFile->write(reinterpret_cast<char*>(&it.second.dataEnd), 8);
		}

		// Go to the table offset
		archiveFile->seekp(5);

		// Write in the new table offset
		archiveFile->write(reinterpret_cast<char*>(&tableOffset), 8);

		// Write and close the file
		archiveFile->flush();
		archiveFile->close();
		delete(archiveFile);
		archiveFile = nullptr;
	}
};