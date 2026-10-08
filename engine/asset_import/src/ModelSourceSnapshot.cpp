#include "PlutoGE/asset_import/ModelSourceSnapshot.h"

#include <set>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    std::string GetModelTextureSourceKey(const std::filesystem::path &source,
        const ImportedTextureData &texture, std::size_t index)
    {
        auto key = texture.sourcePath.empty() ? "texture/embedded/" + std::to_string(index) :
            "texture/file/" + std::filesystem::path(texture.sourcePath).lexically_relative(source.parent_path()).generic_string();
        return key + "/space/" + std::to_string(static_cast<unsigned>(texture.colorSpace));
    }
    namespace
    {
        std::set<std::filesystem::path> Dependencies(const std::filesystem::path &source,
                                                    const ImportedMeshSourceAsset &imported)
        {
            std::set<std::filesystem::path> paths{std::filesystem::weakly_canonical(source)};
            for (const auto &dependency : imported.sourceDependencies)
                paths.insert(std::filesystem::weakly_canonical(std::filesystem::u8path(dependency)));
            return paths;
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }
    }

    bool ReadModelSourceSnapshot(const std::filesystem::path &source, const MeshImportOptions &options,
                                 ModelSourceSnapshot &snapshot, std::string *errorMessage)
    {
        try
        {
            const auto absolute = std::filesystem::weakly_canonical(source);
            const auto dependencies = Dependencies(absolute, MeshImporter{}.ImportMeshSourceAsset(
                absolute.string(), options, MeshSourceCachePolicy::Bypass));
            ModelSourceSnapshot candidate;
            for (const auto &path : dependencies)
            {
                ArtifactInput input;
                input.identity = path == absolute ? "source" : "dependency/" + Utf8(path.lexically_relative(absolute.parent_path()));
                input.path = path;
                if (!content::HashFileContent(path, input.digest, errorMessage)) return false;
                candidate.inputs.push_back(std::move(input));
            }
            candidate.imported = MeshImporter{}.ImportMeshSourceAsset(absolute.string(), options, MeshSourceCachePolicy::Bypass);
            if (Dependencies(absolute, candidate.imported) != dependencies)
                throw std::runtime_error("Model dependency set changed during import; retry after saving completes.");
            ArtifactRecipe validation;
            validation.inputs = candidate.inputs;
            if (!AreArtifactInputsCurrent(validation, errorMessage)) return false;
            snapshot = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Cannot capture model inputs: ") + exception.what();
            return false;
        }
    }
}
