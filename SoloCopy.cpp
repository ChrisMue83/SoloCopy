#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <cstddef>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace fs = std::filesystem;

// Function to retrieve the system's page or block size
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

// Function to compute the hash value of a file
std::string computeHash(const fs::path& filePath) {
    size_t bufferSize = calculateOptimalBufferSize();
    std::vector<unsigned char> buffer(bufferSize);
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (mdctx == nullptr) {
        std::cerr << "Error initializing the digest context." << std::endl;
        return "";
    }

    if (EVP_DigestInit_ex(mdctx, EVP_md5(), nullptr) != 1) {
        std::cerr << "Error initializing the digest." << std::endl;
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening the file: " << filePath << std::endl;
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    while (file.read(reinterpret_cast<char*>(buffer.data()), bufferSize) || file.gcount() > 0) {
        if (EVP_DigestUpdate(mdctx, buffer.data(), file.gcount()) != 1) {
            std::cerr << "Error updating the digest." << std::endl;
            EVP_MD_CTX_free(mdctx);
            return "";
        }
    }

    if (EVP_DigestFinal_ex(mdctx, md_value, &md_len) != 1) {
        std::cerr << "Error finalizing the digest." << std::endl;
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    EVP_MD_CTX_free(mdctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < md_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)md_value[i];
    }
    return ss.str();
}

// Function to check if an entry is a symbolic link
bool isSymlink(const fs::directory_entry& entry) {
    return fs::is_symlink(entry.symlink_status());
}

// Custom function to copy a file with dynamic buffer size and manual attribute transfer
bool copyFileWithAttributesCustom(const fs::path& source, const fs::path& destination) {
    try {
        size_t bufferSize = calculateOptimalBufferSize();
        std::vector<char> buffer(bufferSize);

        std::ifstream src(source, std::ios::binary);
        if (!src) {
            std::cerr << "Error opening the source file: " << source << std::endl;
            return false;
        }

        std::ofstream dest(destination, std::ios::binary);
        if (!dest) {
            std::cerr << "Error creating the destination file: " << destination << std::endl;
            return false;
        }

        while (src.read(buffer.data(), bufferSize) || src.gcount() > 0) {
            dest.write(buffer.data(), src.gcount());
            if (!dest) {
                std::cerr << "Error writing to the destination file: " << destination << std::endl;
                return false;
            }
        }

        // Transfer file permissions
        fs::permissions(destination, fs::status(source).permissions());

        // Transfer last write time
        auto ftime = fs::last_write_time(source);
        fs::last_write_time(destination, ftime);

        // Other attributes like owner and group are platform-specific and require additional implementations

        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error copying the file: " << e.what() << std::endl;
        return false;
    }
}

// Function to scan the output directory and group files by size
void scanOutputDirectory(const fs::path& outputPath,
                         std::unordered_map<std::uintmax_t, std::vector<fs::path>>& outputSizeToFiles) {
    for (const auto& entry : fs::directory_iterator(outputPath)) {
        if (!fs::is_regular_file(entry)) continue;

        std::uintmax_t size = fs::file_size(entry.path());
        outputSizeToFiles[size].emplace_back(entry.path());
    }
}

// Function to scan the input directory and group files by size
void scanInputDirectory(const fs::path& inputPath,
                        std::unordered_map<std::uintmax_t, std::vector<fs::path>>& inputSizeToFiles,
                        int& filesSkipped,
                        int& symlinksCount) {
    for (const auto& entry : fs::recursive_directory_iterator(inputPath)) {
        if (isSymlink(entry)) {
            symlinksCount++;
            continue;
        }
        if (!fs::is_regular_file(entry)) {
            filesSkipped++;
            continue;
        }
        std::uintmax_t size = fs::file_size(entry.path());
        inputSizeToFiles[size].emplace_back(entry.path());
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

// Function to process files without multithreading
void processFiles(const std::unordered_map<std::uintmax_t, std::vector<fs::path>>& inputSizeToFiles,
                 const fs::path& outputPath,
                 const std::unordered_map<std::uintmax_t, std::vector<fs::path>>& outputSizeToFiles,
                 int& filesCopied,
                 std::unordered_map<std::uintmax_t, std::unordered_set<std::string>>& outputHashesCache) {
    for (const auto& [size, inputFiles] : inputSizeToFiles) {
        if (inputFiles.empty()) continue;

        bool isUniqueInInput = (inputFiles.size() == 1);
        bool sizeExistsInOutput = (outputSizeToFiles.find(size) != outputSizeToFiles.end());

        if (isUniqueInInput && !sizeExistsInOutput) {
            // Unique file size and no matching size in the output directory, copy without hash
            const fs::path& file = inputFiles[0];
            fs::path destination = generateUniqueDestination(outputPath, file.filename());

            if (copyFileWithAttributesCustom(file, destination)) {
                filesCopied++;
                // No need to update hash as the size is unique
            }
        } else {
            // Multiple files with the same size or a file with the same size in the output directory
            // Compute hashes only when necessary

            // First, if the size exists in the output, ensure hashes for this size are computed
            if (sizeExistsInOutput && outputHashesCache.find(size) == outputHashesCache.end()) {
                // Compute and cache hashes for output files of this size
                std::unordered_set<std::string> hashes;
                for (const auto& outputFile : outputSizeToFiles.at(size)) {
                    std::string hash = computeHash(outputFile);
                    if (!hash.empty()) {
                        hashes.insert(hash);
                    }
                }
                outputHashesCache[size] = std::move(hashes);
            }

            for (const auto& file : inputFiles) {
                std::string fullHash = computeHash(file);

                if (fullHash.empty()) {
                    // Error computing hash, skip
                    continue;
                }

                // Check if the hash already exists in the output directory
                bool isDuplicate = false;
                if (outputHashesCache.find(size) != outputHashesCache.end()) {
                    if (outputHashesCache[size].find(fullHash) != outputHashesCache[size].end()) {
                        isDuplicate = true;
                    }
                }

                if (isDuplicate) {
                    // File is a duplicate, do not copy
                    continue;
                }

                // Generate a unique destination path
                fs::path destination = generateUniqueDestination(outputPath, file.filename());

                if (copyFileWithAttributesCustom(file, destination)) {
                    filesCopied++;
                    // Add the new hash to the cache
                    outputHashesCache[size].insert(fullHash);
                }
            }
        }
    }
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

    // Initialize counters
    int filesCopied = 0;
    int filesSkipped = 0;
    int symlinksCount = 0;

    // Scan the output directory
    std::unordered_map<std::uintmax_t, std::vector<fs::path>> outputSizeToFiles;
    scanOutputDirectory(outputPath, outputSizeToFiles);

    // Scan the input directory
    std::unordered_map<std::uintmax_t, std::vector<fs::path>> inputSizeToFiles;
    scanInputDirectory(inputPath, inputSizeToFiles, filesSkipped, symlinksCount);

    // Data structures for hash caches
    std::unordered_map<std::uintmax_t, std::unordered_set<std::string>> outputHashesCache;

    // Process files without multithreading
    processFiles(inputSizeToFiles, outputPath, outputSizeToFiles, filesCopied, outputHashesCache);

    // Calculate the total number of files in the input directory
    int totalInputFiles = 0;
    for (const auto& [size, files] : inputSizeToFiles) {
        totalInputFiles += files.size();
    }

    // Calculate the total number of files in the output directory
    int totalOutputFiles = 0;
    for (const auto& [size, files] : outputSizeToFiles) {
        totalOutputFiles += files.size();
    }

    // Output summary
    std::cout << "Processing completed." << std::endl;
    std::cout << "Total files in input directory: " << totalInputFiles << std::endl;
    std::cout << "Total files in output directory: " << totalOutputFiles << std::endl;
    std::cout << "Number of files actually copied: " << filesCopied << std::endl;
    std::cout << "Number of symlinks in input directory: " << symlinksCount << std::endl;
    std::cout << "Number of skipped files (non-regular files): " << filesSkipped << std::endl;

    return 0;
}
