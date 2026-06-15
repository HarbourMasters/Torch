#include "IntegrationTestHelpers.h"
#include "Companion.h"

// Forward-declare miniz C API functions we need (defined in zip_file.hpp, compiled via ZWrapper.cpp)
extern "C" {
typedef unsigned int mz_uint;
typedef int mz_bool;
typedef struct mz_zip_archive_tag mz_zip_archive;

struct mz_zip_internal_state_tag;

struct mz_zip_archive_tag {
    unsigned long long m_archive_size;
    unsigned long long m_central_directory_file_ofs;
    mz_uint m_total_files;
    int m_zip_mode;
    int m_zip_type;
    int m_last_error;
    unsigned long long m_file_offset_alignment;
    void *(*m_pAlloc)(void *, size_t, size_t);
    void *(*m_pRealloc)(void *, void *, size_t, size_t);
    void (*m_pFree)(void *, void *);
    void *m_pAlloc_opaque;
    size_t (*m_pRead)(void *, unsigned long long, void *, size_t);
    size_t (*m_pWrite)(void *, unsigned long long, const void *, size_t);
    void *m_pIO_opaque;
    struct mz_zip_internal_state_tag *m_pState;
};

mz_bool mz_zip_reader_init_file(mz_zip_archive *pZip, const char *pFilename, mz_uint flags);
mz_uint mz_zip_reader_get_num_files(mz_zip_archive *pZip);
mz_uint mz_zip_reader_get_filename(mz_zip_archive *pZip, mz_uint file_index, char *pFilename, mz_uint filename_buf_size);
void *mz_zip_reader_extract_to_heap(mz_zip_archive *pZip, mz_uint file_index, size_t *pSize, mz_uint flags);
mz_bool mz_zip_reader_end(mz_zip_archive *pZip);
void mz_free(void *p);
}

std::map<std::string, std::vector<char>> RunPipeline(const std::string& configDir, const std::string& romPath) {
    auto tmpDir = fs::temp_directory_path() / "torch_integration_test";
    if (fs::exists(tmpDir)) {
        fs::remove_all(tmpDir);
    }
    fs::create_directories(tmpDir);

    auto instance = Companion::Instance = new Companion(
        fs::path(romPath), ArchiveType::O2R, false, configDir, tmpDir.string()
    );
    instance->Init(ExportType::Binary);

    // After Process() completes, Companion cleans up Instance, AudioManager, and Decompressor cache.
    // Find the output .o2r file
    std::string o2rPath;
    for (auto& entry : fs::recursive_directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".o2r") {
            o2rPath = entry.path().string();
            break;
        }
    }

    std::map<std::string, std::vector<char>> assets;
    if (!o2rPath.empty()) {
        mz_zip_archive zip{};
        if (mz_zip_reader_init_file(&zip, o2rPath.c_str(), 0)) {
            mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
            for (mz_uint i = 0; i < numFiles; i++) {
                char filename[512];
                mz_zip_reader_get_filename(&zip, i, filename, sizeof(filename));

                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
                if (data) {
                    auto* bytes = static_cast<char*>(data);
                    assets[filename] = std::vector<char>(bytes, bytes + size);
                    mz_free(data);
                }
            }
            mz_zip_reader_end(&zip);
        }
    }

    fs::remove_all(tmpDir);
    return assets;
}
