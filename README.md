# SoloCopy
Recursively scans a nested input directory and copies unique files by size and hash into a flat output directory, preserving all file attributes.


Program Description and Usage Guide
Description
This program is a file deduplication and management tool. It scans an input directory for files, processes them, and copies unique files to an output directory while ensuring that duplicates are not copied. The uniqueness of files is determined by their size and content hash (MD5). The program handles symbolic links, skips non-regular files, and ensures that attributes (like permissions and timestamps) of the files are preserved during copying.

Features
Hash-based Deduplication: Compares files based on their MD5 hash to detect duplicates.
Attribute Preservation: Maintains file permissions and timestamps when copying files.
Symbolic Link Handling: Counts symbolic links in the input directory but does not process them.
Error Handling: Provides feedback if errors occur during file processing or copying.
Unique File Naming: Ensures that files with the same name in the output directory are renamed with unique suffixes.
How to Use
Compile the Program Ensure you have a C++ compiler and OpenSSL installed. Use the following command to compile the program:


Run the Program Execute the compiled program with two arguments:
The path to the input directory (where files to be processed are located).
The path to the output directory (where unique files will be copied).
Example:

bash
Code kopieren
./SoloCopy /path/to/input /path/to/output
Output The program will:

Print the total number of files in the input and output directories.
Indicate how many files were copied.
Report how many symbolic links and non-regular files were skipped.
