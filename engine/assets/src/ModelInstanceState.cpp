#include "PlutoGE/assets/ModelInstanceState.h"
#include "PlutoGE/assets/AssetReferences.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <set>
#include <stdexcept>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Magic = "PLUTOMODELINSTANCE";
        constexpr std::size_t MaxBytes = 32 * 1024 * 1024;
        constexpr std::size_t MaxString = 1024 * 1024;
        constexpr std::size_t MaxCount = 4096;

        void Validate(const StaticModelInstanceState &state)
        {
            StaticModelInstanceReconciliation check;
            std::string error;
            if (!PrepareStaticModelInstanceReconciliation(state.accepted, state.overrides, state.accepted, check, &error))
                throw std::runtime_error(error);
            if (!state.rootEntityId || std::none_of(state.artifactGenerationKey.begin(), state.artifactGenerationKey.end(),
                [](auto byte) { return byte != 0; }) || state.nodeEntities.size() != state.accepted.layout.nodes.size() ||
                state.bindingEntities.size() != state.accepted.layout.bindings.size() ||
                state.defaultMaterials.size() != state.accepted.materialSlotCount)
                throw std::runtime_error("Incomplete model instance baseline, generation or entity mapping.");
            const auto &package = state.packageArtifact;
            const auto path = std::filesystem::path(package.reference.substr(
                package.reference.starts_with(Project::kProjectAssetScheme) ? Project::kProjectAssetScheme.size() : 0));
            if (!package.reference.starts_with(Project::kProjectAssetScheme) || NormalizeAssetReference(package.reference) != package.reference ||
                path.has_root_path() || path.extension() != ".plutomodel" ||
                std::any_of(path.begin(), path.end(), [](const auto &part) { return part == ".." || part == "." || part.empty(); }) ||
                std::none_of(package.digest.begin(), package.digest.end(), [](auto byte) { return byte != 0; }))
                throw std::runtime_error("Model instance requires an exact accepted package descriptor.");
            std::set<std::uint64_t> expectedNodes, mappedNodes;
            std::set<std::uint32_t> entities{state.rootEntityId};
            for (const auto &node : state.accepted.layout.nodes) expectedNodes.insert(node.sourceNodeId);
            for (const auto &mapping : state.nodeEntities)
                if (!expectedNodes.contains(mapping.sourceNodeId) || !mappedNodes.insert(mapping.sourceNodeId).second ||
                    !mapping.sceneEntityId || !entities.insert(mapping.sceneEntityId).second)
                    throw std::runtime_error("Unknown/duplicate source node or scene entity in model instance mapping.");
            for (const auto entity : state.bindingEntities)
                if (!entity || !entities.insert(entity).second)
                    throw std::runtime_error("Invalid/duplicate geometry entity in model instance mapping.");
            for (const auto &reference : state.defaultMaterials)
                if (reference.size() > MaxString || (!reference.empty() && NormalizeAssetReference(reference).empty()))
                    throw std::runtime_error("Invalid model instance default material reference.");
        }
        struct Writer
        {
            std::string bytes;
            void Add(std::string_view value)
            {
                if (value.size() > MaxBytes - bytes.size()) throw std::runtime_error("Model instance exceeds its byte limit.");
                bytes.append(value);
            }
            void Number(std::uint64_t value, unsigned width)
            {
                for (unsigned byte = 0; byte < width; ++byte)
                {
                    const char part = static_cast<char>(value >> (byte * 8)); Add({&part, 1});
                }
            }
            void Text(std::string_view value)
            {
                if (value.size() > MaxString) throw std::runtime_error("Model instance string exceeds its limit.");
                Number(value.size(), 4); Add(value);
            }
            void Digest(const content::ContentDigest &value)
            {
                Add({reinterpret_cast<const char *>(value.data()), value.size()});
            }
            void Vector(const glm::vec3 &value)
            {
                for (int axis = 0; axis < 3; ++axis) Number(std::bit_cast<std::uint32_t>(value[axis]), 4);
            }
            void Matrix(const glm::mat4 &value)
            {
                static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row) Number(std::bit_cast<std::uint32_t>(value[column][row]), 4);
            }
        };
        struct Reader
        {
            std::string_view bytes;
            std::uint64_t Number(unsigned width)
            {
                if (bytes.size() < width) throw std::runtime_error("Truncated model instance payload.");
                std::uint64_t result = 0;
                for (unsigned byte = 0; byte < width; ++byte)
                    result |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[byte])) << (8 * byte);
                bytes.remove_prefix(width); return result;
            }
            std::size_t Count(std::size_t limit = MaxCount)
            {
                const auto count = Number(4);
                if (count > limit) throw std::runtime_error("Model instance inventory exceeds its limit.");
                return static_cast<std::size_t>(count);
            }
            std::string Text()
            {
                const auto size = Count(MaxString);
                if (bytes.size() < size) throw std::runtime_error("Truncated model instance string.");
                std::string result(bytes.substr(0, size)); bytes.remove_prefix(size); return result;
            }
            content::ContentDigest Digest()
            {
                content::ContentDigest result{};
                for (auto &byte : result) byte = static_cast<std::uint8_t>(Number(1));
                return result;
            }
            glm::vec3 Vector()
            {
                glm::vec3 result;
                for (int axis = 0; axis < 3; ++axis) result[axis] = std::bit_cast<float>(static_cast<std::uint32_t>(Number(4)));
                return result;
            }
            glm::mat4 Matrix()
            {
                glm::mat4 result(1);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row) result[column][row] = std::bit_cast<float>(static_cast<std::uint32_t>(Number(4)));
                return result;
            }
        };
    }

    bool SerializeStaticModelInstanceState(const StaticModelInstanceState &state, std::string &bytes, std::string *errorMessage)
    {
        try
        {
            Validate(state);
            Writer writer;
            const bool granular = std::any_of(state.overrides.nodes.begin(), state.overrides.nodes.end(),
                [](const auto &node) { return node.localPosition || node.localRotation || node.localScale; });
            writer.Add(Magic); writer.Number(granular ? 4 : 3, 4); writer.Number(state.rootEntityId, 4);
            writer.Digest(state.artifactGenerationKey);
            writer.Text(state.packageArtifact.reference); writer.Digest(state.packageArtifact.digest);
            writer.Digest(state.accepted.meshDigest); writer.Digest(state.accepted.layout.hierarchyDigest);
            const auto &layout = state.accepted.layout;
            writer.Text(layout.sourceAssetId); writer.Text(layout.meshReference);
            writer.Number(state.accepted.submeshCount, 4); writer.Number(state.accepted.materialSlotCount, 4);
            writer.Number(layout.nodes.size(), 4);
            for (const auto &node : layout.nodes)
            {
                writer.Number(node.sourceNodeId, 8); writer.Text(node.name);
                writer.Number(node.parentIndex < 0 ? UINT32_MAX : static_cast<std::uint32_t>(node.parentIndex), 4);
                writer.Matrix(node.localTransform);
            }
            writer.Number(layout.bindings.size(), 4);
            for (const auto &binding : layout.bindings)
            {
                writer.Number(binding.nodeIndex, 4); writer.Number(binding.submeshIndex, 4); writer.Matrix(binding.geometryToNode);
            }
            writer.Number(state.defaultMaterials.size(), 4);
            for (const auto &reference : state.defaultMaterials) writer.Text(reference);
            writer.Digest(state.overrides.hierarchyDigest); writer.Digest(state.overrides.meshDigest);
            writer.Number(state.overrides.nodes.size(), 4);
            for (const auto &node : state.overrides.nodes)
            {
                writer.Number(node.sourceNodeId, 8);
                const unsigned flags = (node.localTransform ? 1u : 0u) | (node.enabled.has_value() ? 2u : 0u) |
                    (node.hasAdditionalEdits ? 4u : 0u) | (node.hasStructuralEdits ? 8u : 0u) | (node.hasGeometryEdits ? 16u : 0u) |
                    (node.localPosition ? 32u : 0u) | (node.localRotation ? 64u : 0u) | (node.localScale ? 128u : 0u);
                writer.Number(flags, 1);
                if (node.localTransform) writer.Matrix(*node.localTransform);
                if (node.enabled) writer.Number(*node.enabled ? 1 : 0, 1);
                if (node.localPosition) writer.Vector(*node.localPosition);
                if (node.localRotation) writer.Vector(*node.localRotation);
                if (node.localScale) writer.Vector(*node.localScale);
            }
            writer.Number(state.overrides.materials.size(), 4);
            for (const auto &material : state.overrides.materials)
            {
                writer.Number(material.bindingIndex, 4); writer.Number(material.materialSlot, 4); writer.Text(material.reference);
            }
            writer.Number(state.nodeEntities.size(), 4);
            for (const auto &node : state.nodeEntities) { writer.Number(node.sourceNodeId, 8); writer.Number(node.sceneEntityId, 4); }
            writer.Number(state.bindingEntities.size(), 4);
            for (const auto entity : state.bindingEntities) writer.Number(entity, 4);
            bytes = std::move(writer.bytes);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = std::string("Cannot serialize model instance: ") + error.what();
            return false;
        }
    }

    bool ParseStaticModelInstanceState(std::string_view bytes, StaticModelInstanceState &state, std::string *errorMessage)
    {
        try
        {
            if (bytes.size() > MaxBytes || !bytes.starts_with(Magic)) throw std::runtime_error("Invalid model instance payload header/size.");
            Reader reader{bytes.substr(Magic.size())};
            const auto version = reader.Number(4);
            if (version != 2 && version != 3 && version != 4) throw std::runtime_error("Unsupported model instance payload version.");
            StaticModelInstanceState candidate;
            candidate.rootEntityId = static_cast<std::uint32_t>(reader.Number(4));
            candidate.artifactGenerationKey = reader.Digest();
            candidate.packageArtifact.reference = reader.Text(); candidate.packageArtifact.digest = reader.Digest();
            candidate.accepted.meshDigest = reader.Digest(); candidate.accepted.layout.hierarchyDigest = reader.Digest();
            auto &layout = candidate.accepted.layout;
            layout.sourceAssetId = reader.Text(); layout.meshReference = reader.Text();
            candidate.accepted.submeshCount = reader.Count(100000); candidate.accepted.materialSlotCount = reader.Count(100000);
            auto count = reader.Count(); layout.nodes.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                StaticModelInstanceNode node;
                node.sourceNodeId = reader.Number(8); node.name = reader.Text();
                const auto parent = reader.Number(4);
                if (parent != UINT32_MAX && parent >= index) throw std::runtime_error("Invalid model instance parent address.");
                node.parentIndex = parent == UINT32_MAX ? -1 : static_cast<int>(parent);
                node.localTransform = reader.Matrix(); layout.nodes.push_back(std::move(node));
            }
            count = reader.Count(); layout.bindings.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                StaticModelInstanceBinding binding;
                const auto node = reader.Number(4);
                if (node >= layout.nodes.size()) throw std::runtime_error("Invalid model instance binding address.");
                binding.nodeIndex = static_cast<int>(node); binding.submeshIndex = static_cast<std::uint32_t>(reader.Number(4));
                binding.geometryToNode = reader.Matrix(); layout.bindings.push_back(binding);
            }
            count = reader.Count(100000); candidate.defaultMaterials.reserve(count);
            for (std::size_t index = 0; index < count; ++index) candidate.defaultMaterials.push_back(reader.Text());
            candidate.overrides.hierarchyDigest = reader.Digest(); candidate.overrides.meshDigest = reader.Digest();
            count = reader.Count(); candidate.overrides.nodes.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                StaticModelNodeOverride node;
                node.sourceNodeId = reader.Number(8);
                const auto flags = reader.Number(1);
                if (flags & ~(version == 4 ? 255ull : (version == 3 ? 31ull : 15ull))) throw std::runtime_error("Unsupported model instance override flags.");
                if (flags & 1) node.localTransform = reader.Matrix();
                if (flags & 2)
                {
                    const auto enabled = reader.Number(1);
                    if (enabled > 1) throw std::runtime_error("Invalid model instance enabled override.");
                    node.enabled = enabled != 0;
                }
                node.hasAdditionalEdits = (flags & 4) != 0; node.hasStructuralEdits = (flags & 8) != 0;
                node.hasGeometryEdits = version >= 3 ? (flags & 16) != 0 : node.hasAdditionalEdits;
                if (flags & 32) node.localPosition = reader.Vector();
                if (flags & 64) node.localRotation = reader.Vector();
                if (flags & 128) node.localScale = reader.Vector();
                candidate.overrides.nodes.push_back(std::move(node));
            }
            count = reader.Count(); candidate.overrides.materials.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                StaticModelMaterialOverride material;
                material.bindingIndex = static_cast<std::size_t>(reader.Number(4)); material.materialSlot = static_cast<std::size_t>(reader.Number(4));
                material.reference = reader.Text(); candidate.overrides.materials.push_back(std::move(material));
            }
            count = reader.Count(); candidate.nodeEntities.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto source = reader.Number(8); const auto entity = static_cast<std::uint32_t>(reader.Number(4));
                candidate.nodeEntities.push_back({source, entity});
            }
            count = reader.Count(); candidate.bindingEntities.reserve(count);
            for (std::size_t index = 0; index < count; ++index) candidate.bindingEntities.push_back(static_cast<std::uint32_t>(reader.Number(4)));
            if (!reader.bytes.empty()) throw std::runtime_error("Unexpected trailing model instance data.");
            Validate(candidate);
            state = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = std::string("Cannot parse model instance: ") + error.what();
            return false;
        }
    }
}
