#include "hot_load_task.h"

#include <filesystem>

std::string HotLoadTask::normalize_path(const std::string& input_path) {
    namespace fs = std::filesystem;

    try {
        if (!fs::exists(input_path) || !fs::is_regular_file(input_path)) {
            return {}; // Return empty string if path does not exist
        }

        // Convert to absolute path (if input is relative)
        fs::path absolute_path = fs::absolute(input_path);
        
        // Resolve symbolic links and eliminate redundancies
        fs::path canonical_path = fs::weakly_canonical(absolute_path);
        
        // Normalize the path format
        canonical_path = canonical_path.lexically_normal();
        canonical_path.make_preferred(); // Convert slashes to preferred format

        return canonical_path.string();
    } catch (const fs::filesystem_error& e) {
        return {}; // Return empty string on error
    }
}