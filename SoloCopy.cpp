#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <array>
#include <iomanip>
#include <sstream>
#include <cstddef>
#include <functional>
#include <omp.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

// Verwenden von xxHash für schnellere Hash-Berechnungen
#define XXH_INLINE_ALL
#include "xxhash.h"

namespace fs = std::filesystem;

// Custom hash function for std::array<unsigned char, 8>
struct ArrayHash {
    std::size_t operator()(const std::array<unsigned char, 8>& arr) const {
        std::size_t hash = 0;
        for (const auto& byte : arr) {
            hash = hash * 31 + byte;
        }
        return hash;
    }
};

// Function to retrieve the system's page size
size_t getSystemPageSize() {
    size_t page_size = 4096; // Default value

    #ifdef _WIN32
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        page_size = sysInfo.dwPageSize;
    #else
        long res = sysconf(_SC_PAGESIZE);
        if (res > 0) {
            page_size = static_cast<size_t>(res);
        }
    #endif

    return page_size;
}

// Function to calculate the optimal buffer size
size_t calculateOptimalBufferSize() {
    size_t page_size = getSystemPageSize();
    size_t multiplier = 256; // Buffer size = 256 * page/block size

    size_t buffer_size = page_size * multiplier;

    // Optional: Set a maximum buffer size, e.g., 8 MB
    size_t max_buffer_size = 8 * 1024 * 1024; // 8 MB
    if (buffer_size > max_buffer_size) {
        buffer_size = max_buffer_size;
    }

    return buffer_size;
}

// Function to compute a partial hash of a file
std::array<unsigned char, 8> computePartialHash(const fs::path& filePath) {
    size_t bufferSize = 64 * 1024; // Read first and last 64 KB
    std::vector<unsigned char> buffer(bufferSize * 2);

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        // Handle error
        return {};
    }

    // Read first 64 KB
    file.read(reinterpret_cast<char*>(buffer.data()), bufferSize);
    size_t bytesRead = file.gcount();

    // Read last 64 KB
    file.seekg(-static_cast<std::streamoff>(bufferSize), std::ios::end);
    file.read(reinterpret_cast<char*>(buffer.data() + bytesRead), bufferSize);
    size_t totalBytesRead = bytesRead + file.gcount();

    // Compute hash
    XXH64_hash_t hash = XXH64(buffer.data(), totalBytesRead, 0);

    std::array<unsigned char, 8> hashArr;
    for (int i = 0; i < 8; ++i) {
        hashArr[i] = (hash >> (i * 8)) & 0xFF;
    }

    return hashArr;
}

// Function to compute the full hash of a file
std::array<unsigned char, 8> computeFullHash(const fs::path& filePath) {
    size_t bufferSize = calculateOptimalBufferSize();
    std::vector<unsigned char> buffer(bufferSize);

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        // Handle error
        return {};
    }

    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    while (file.read(reinterpret_cast<char*>(buffer.data()), bufferSize) || file.gcount() > 0) {
        XXH64_update(state, buffer.data(), file.gcount());
    }

    XXH64_hash_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    std::array<unsigned char, 8> hashArr;
    for (int i = 0; i < 8; ++i) {
        hashArr[i] = (hash >> (i * 8)) & 0xFF;
    }

    return hashArr;
}

// Function to check if an entry is a symbolic link
bool isSymlink(const fs::directory_entry& entry) {
    return fs::is_symlink(entry.symlink_status());
}

// Function to check if one path is a subpath of another
bool isSubPath(const fs::path& parent, const fs::path& child) {
    auto parentAbs = fs::canonical(parent);
    auto childAbs = fs::canonical(child);

    // If parent and child are the same, return true
    if (parentAbs == childAbs) {
        return true;
    }

    // Check if child path starts with parent path
    auto mismatchPair = std::mismatch(parentAbs.begin(), parentAbs.end(), childAbs.begin());
    return mismatchPair.first == parentAbs.end();
}

// Function to scan a directory and collect file sizes and hashes
void scanDirectory(
    const fs::path& dirPath,
    std::unordered_map<std::uintmax_t, std::vector<fs::path>>& sizeToFiles,
    std::unordered_map<fs::path, std::array<unsigned char, 8>>& fileHashes,
    int& filesSkipped,
    int& symlinksCount
) {
    std::vector<fs::path> allFiles;

    // Collect all files first
    for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
        if (isSymlink(entry)) {
            symlinksCount++;
            continue;
        }
        if (!fs::is_regular_file(entry)) {
            filesSkipped++;
            continue;
        }
        allFiles.emplace_back(entry.path());
    }

    // Parallelize hash computation
    size_t numFiles = allFiles.size();

    // Thread-local data
    int numThreads = omp_get_max_threads();
    std::vector<std::unordered_map<std::uintmax_t, std::vector<fs::path>>> sizeToFilesPerThread(numThreads);
    std::vector<std::unordered_map<fs::path, std::array<unsigned char, 8>>> fileHashesPerThread(numThreads);

    #pragma omp parallel
    {
        int threadId = omp_get_thread_num();
        auto& localSizeToFiles = sizeToFilesPerThread[threadId];
        auto& localFileHashes = fileHashesPerThread[threadId];

        #pragma omp for nowait
        for (size_t i = 0; i < numFiles; ++i) {
            const fs::path& filePath = allFiles[i];
            std::uintmax_t size = fs::file_size(filePath);

            // Compute partial hash
            std::array<unsigned char, 8> hashValue = computePartialHash(filePath);

            // Add to thread-local structures
            localSizeToFiles[size].emplace_back(filePath);
            localFileHashes[filePath] = hashValue;
        }
    }

    // Merge thread-local data into global structures
    for (int i = 0; i < numThreads; ++i) {
        for (const auto& [size, paths] : sizeToFilesPerThread[i]) {
            sizeToFiles[size].insert(sizeToFiles[size].end(), paths.begin(), paths.end());
        }
        fileHashes.insert(fileHashesPerThread[i].begin(), fileHashesPerThread[i].end());
    }
}

// Function to generate a unique destination path
fs::path generateUniqueDestination(const fs::path& outputPath, const fs::path& originalName) {
    fs::path destination = outputPath / originalName;
    if (!fs::exists(destination)) {
        return destination;
    }

    int suffix = 1;
    fs::path newDestination;
    do {
        newDestination = outputPath / (originalName.stem().string() + "_" + std::to_string(suffix) + originalName.extension().string());
        suffix++;
    } while (fs::exists(newDestination));
    return newDestination;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input directory> <output directory>" << std::endl;
        return 1;
    }

    fs::path inputPath = argv[1];
    fs::path outputPath = argv[2];

    if (!fs::exists(inputPath) || !fs::is_directory(inputPath)) {
        std::cerr << "Invalid input directory." << std::endl;
        return 1;
    }

    if (!fs::exists(outputPath)) {
        fs::create_directories(outputPath);
    } else if (!fs::is_directory(outputPath)) {
        std::cerr << "The output path is not a directory." << std::endl;
        return 1;
    }

    // Ensure that input and output directories are not the same and not nested
    if (fs::equivalent(inputPath, outputPath)) {
        std::cerr << "Input and output directories cannot be the same." << std::endl;
        return 1;
    }

    if (isSubPath(inputPath, outputPath) || isSubPath(outputPath, inputPath)) {
        std::cerr << "Input and output directories must not be nested within each other." << std::endl;
        return 1;
    }

    // Initialize counters
    int filesCopied = 0;
    int filesSkippedInput = 0;
    int symlinksCountInput = 0;
    int filesSkippedOutput = 0;
    int symlinksCountOutput = 0;

    // Data structures for input and output directories
    std::unordered_map<std::uintmax_t, std::vector<fs::path>> inputSizeToFiles;
    std::unordered_map<fs::path, std::array<unsigned char, 8>> inputFileHashes;

    std::unordered_map<std::uintmax_t, std::vector<fs::path>> outputSizeToFiles;
    std::unordered_map<fs::path, std::array<unsigned char, 8>> outputFileHashes;

    // Scan and compute hashes for the output directory
    std::cout << "Scanning and hashing output directory..." << std::endl;
    scanDirectory(outputPath, outputSizeToFiles, outputFileHashes, filesSkippedOutput, symlinksCountOutput);
    std::cout << "Finished scanning and hashing output directory." << std::endl;
    int totalOutputFiles = 0;
    for (const auto& [size, files] : outputSizeToFiles) {
        totalOutputFiles += files.size();
    }
    std::cout << "Total files in output directory: " << totalOutputFiles << std::endl << std::endl;

    // Scan and compute hashes for the input directory
    std::cout << "Scanning and hashing input directory..." << std::endl;
    scanDirectory(inputPath, inputSizeToFiles, inputFileHashes, filesSkippedInput, symlinksCountInput);
    std::cout << "Finished scanning and hashing input directory." << std::endl;
    int totalInputFiles = 0;
    for (const auto& [size, files] : inputSizeToFiles) {
        totalInputFiles += files.size();
    }
    std::cout << "Total files in input directory: " << totalInputFiles << std::endl << std::endl;

    // After all data is collected, begin processing
    std::cout << "Starting file comparison and copying..." << std::endl;

    int progressCounter = 0;
    int totalFilesToProcess = totalInputFiles;
    int progressStep = totalFilesToProcess / 100; // For percentage progress
    if (progressStep == 0) progressStep = 1; // Ensure progressStep is at least 1

    // Set to keep track of already copied hashes to avoid duplicates from input directory
    std::unordered_set<std::array<unsigned char, 8>, ArrayHash> copiedHashes;

    int duplicateFilesSkipped = 0; // Counter for duplicates in input directory

    // Vector to store files to copy
    std::vector<std::pair<fs::path, fs::path>> filesToCopy;

    // Process files
    for (const auto& [size, inputFiles] : inputSizeToFiles) {
        // Check if size exists in output directory
        bool sizeExistsInOutput = (outputSizeToFiles.find(size) != outputSizeToFiles.end());

        for (const auto& file : inputFiles) {
            progressCounter++;

            // Progress indicator
            if (progressCounter % (progressStep * 10) == 0 || progressCounter == totalFilesToProcess) {
                int percent = (progressCounter * 100) / totalFilesToProcess;
                std::cout << "Progress: " << percent << "% (" << progressCounter << "/" << totalFilesToProcess << " files processed)" << "\r" << std::flush;
            }

            // Get partial hash from inputFileHashes
            std::array<unsigned char, 8> inputHash = inputFileHashes[file];

            bool isDuplicate = false;

            // Check if the hash has already been copied from input directory
            if (copiedHashes.find(inputHash) != copiedHashes.end()) {
                isDuplicate = true;
                duplicateFilesSkipped++;
            }
            // Check if hash exists in outputFileHashes
            else if (sizeExistsInOutput) {
                for (const auto& outputFile : outputSizeToFiles[size]) {
                    if (outputFileHashes[outputFile] == inputHash) {
                        isDuplicate = true;
                        break;
                    }
                }
            }

            if (!isDuplicate) {
                // Need to compute full hash to confirm
                std::array<unsigned char, 8> fullInputHash = computeFullHash(file);

                bool fullDuplicate = false;

                // Check in copiedHashes again with full hash
                if (copiedHashes.find(fullInputHash) != copiedHashes.end()) {
                    fullDuplicate = true;
                    duplicateFilesSkipped++;
                } else if (sizeExistsInOutput) {
                    for (const auto& outputFile : outputSizeToFiles[size]) {
                        std::array<unsigned char, 8> fullOutputHash = computeFullHash(outputFile);
                        if (fullOutputHash == fullInputHash) {
                            fullDuplicate = true;
                            break;
                        }
                    }
                }

                if (!fullDuplicate) {
                    fs::path destination = generateUniqueDestination(outputPath, file.filename());
                    filesToCopy.emplace_back(file, destination);
                    copiedHashes.insert(fullInputHash); // Add the full hash to the set of copied hashes
                } else {
                    duplicateFilesSkipped++;
                }
            }
        }
    }

    std::cout << std::endl << "Starting parallel file copying..." << std::endl;

    // Parallel file copying
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < filesToCopy.size(); ++i) {
        const auto& [source, destination] = filesToCopy[i];
        if (fs::copy_file(source, destination, fs::copy_options::overwrite_existing)) {
            #pragma omp atomic
            filesCopied++;
        }
    }

    std::cout << "File copying completed." << std::endl << std::endl;

    // Recalculate the total number of files in the output directory after copying
    totalOutputFiles += filesCopied; // Add the newly copied files

    // Output summary
    std::cout << "Processing completed." << std::endl;
    std::cout << "Total files in input directory: " << totalInputFiles << std::endl;
    std::cout << "Total files in output directory: " << totalOutputFiles << std::endl;
    std::cout << "Number of files actually copied: " << filesCopied << std::endl;
    std::cout << "Number of duplicate files skipped in input directory: " << duplicateFilesSkipped << std::endl;
    std::cout << "Number of symbolic links in input directory: " << symlinksCountInput << std::endl;
    std::cout << "Number of skipped files in input directory (non-regular files): " << filesSkippedInput << std::endl;

    return 0;
}
