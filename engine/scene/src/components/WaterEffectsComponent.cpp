#include "PlutoGE/scene/components/WaterEffectsComponent.h"

#include <charconv>
#include <cstdlib>
#include <functional>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        using Profile = water::WaterEffectsProfile;

        // One serialized property bound to one profile field, so Serialize and Deserialize share a single list.
        struct FieldBinding
        {
            std::string name;
            PropertyType type;
            std::function<std::string(const Profile &)> write;
            std::function<void(Profile &, const std::string &)> read;
        };

        template <typename Access>
        FieldBinding FloatField(std::string name, Access access)
        {
            return {std::move(name), PropertyType::Float,
                    [access](const Profile &profile) { return std::to_string(access(const_cast<Profile &>(profile))); },
                    [access](Profile &profile, const std::string &text) {
                        char *end = nullptr;
                        const float value = std::strtof(text.c_str(), &end);
                        if (end != text.c_str())
                            access(profile) = value;
                    }};
        }

        template <typename Access>
        FieldBinding IntField(std::string name, Access access)
        {
            return {std::move(name), PropertyType::Int,
                    [access](const Profile &profile) { return std::to_string(access(const_cast<Profile &>(profile))); },
                    [access](Profile &profile, const std::string &text) {
                        int value = 0;
                        if (std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{})
                            access(profile) = value;
                    }};
        }

        template <typename Access>
        FieldBinding BoolField(std::string name, Access access)
        {
            return {std::move(name), PropertyType::Bool,
                    [access](const Profile &profile) { return std::string(access(const_cast<Profile &>(profile)) ? "true" : "false"); },
                    [access](Profile &profile, const std::string &text) { access(profile) = text == "true" || text == "1"; }};
        }

        template <typename Access>
        FieldBinding StringField(std::string name, Access access)
        {
            return {std::move(name), PropertyType::String,
                    [access](const Profile &profile) { return access(const_cast<Profile &>(profile)); },
                    [access](Profile &profile, const std::string &text) { access(profile) = text; }};
        }

        void AddSplash(std::vector<FieldBinding> &fields, const std::string &prefix, water::WaterSplashEffect Profile::*member)
        {
            fields.push_back(StringField(prefix + ".Particles", [member](Profile &p) -> std::string & { return (p.*member).particles; }));
            fields.push_back(StringField(prefix + ".HeavyParticles", [member](Profile &p) -> std::string & { return (p.*member).heavyParticles; }));
            fields.push_back(IntField(prefix + ".MinParticles", [member](Profile &p) -> int & { return (p.*member).minCount; }));
            fields.push_back(IntField(prefix + ".MaxParticles", [member](Profile &p) -> int & { return (p.*member).maxCount; }));
        }

        void AddSound(std::vector<FieldBinding> &fields, const std::string &prefix, water::WaterSoundEffect Profile::*member)
        {
            fields.push_back(StringField(prefix + ".Sound", [member](Profile &p) -> std::string & { return (p.*member).sound; }));
            fields.push_back(StringField(prefix + ".HeavySound", [member](Profile &p) -> std::string & { return (p.*member).heavySound; }));
            fields.push_back(FloatField(prefix + ".Volume", [member](Profile &p) -> float & { return (p.*member).volume; }));
            fields.push_back(FloatField(prefix + ".PitchVariation", [member](Profile &p) -> float & { return (p.*member).pitchVariation; }));
            fields.push_back(FloatField(prefix + ".MinDistance", [member](Profile &p) -> float & { return (p.*member).minDistance; }));
            fields.push_back(FloatField(prefix + ".MaxDistance", [member](Profile &p) -> float & { return (p.*member).maxDistance; }));
        }

        const std::vector<FieldBinding> &Fields()
        {
            static const std::vector<FieldBinding> fields = [] {
                std::vector<FieldBinding> list;
                list.push_back(BoolField("Ripples.Enabled", [](Profile &p) -> bool & { return p.ripples.enabled; }));
                list.push_back(FloatField("Ripples.Strength", [](Profile &p) -> float & { return p.ripples.strength; }));
                list.push_back(FloatField("Ripples.Speed", [](Profile &p) -> float & { return p.ripples.speed; }));
                list.push_back(FloatField("Ripples.Wavelength", [](Profile &p) -> float & { return p.ripples.wavelength; }));
                list.push_back(FloatField("Ripples.Lifetime", [](Profile &p) -> float & { return p.ripples.lifetime; }));
                list.push_back(FloatField("Ripples.Foam", [](Profile &p) -> float & { return p.ripples.foam; }));
                AddSplash(list, "EnterSplash", &Profile::enterSplash);
                AddSplash(list, "ExitSplash", &Profile::exitSplash);
                AddSplash(list, "WakeSplash", &Profile::wakeSplash);
                list.push_back(FloatField("MinSplashIntensity", [](Profile &p) -> float & { return p.minSplashIntensity; }));
                AddSound(list, "EnterSound", &Profile::enterSound);
                AddSound(list, "ExitSound", &Profile::exitSound);
                AddSound(list, "WakeSound", &Profile::wakeSound);
                list.push_back(FloatField("MinSoundIntensity", [](Profile &p) -> float & { return p.minSoundIntensity; }));
                list.push_back(FloatField("WakeSoundInterval", [](Profile &p) -> float & { return p.wakeSoundInterval; }));
                list.push_back(FloatField("HeavyIntensity", [](Profile &p) -> float & { return p.heavyIntensity; }));
                list.push_back(StringField("Ambient.Sound", [](Profile &p) -> std::string & { return p.ambientSound; }));
                list.push_back(FloatField("Ambient.Volume", [](Profile &p) -> float & { return p.ambientVolume; }));
                list.push_back(FloatField("Ambient.MinDistance", [](Profile &p) -> float & { return p.ambientMinDistance; }));
                list.push_back(FloatField("Ambient.MaxDistance", [](Profile &p) -> float & { return p.ambientMaxDistance; }));
                list.push_back(FloatField("Underwater.LowPass", [](Profile &p) -> float & { return p.underwaterLowPass; }));
                list.push_back(FloatField("Underwater.Gain", [](Profile &p) -> float & { return p.underwaterGain; }));
                return list;
            }();
            return fields;
        }
    }

    std::vector<Property> WaterEffectsComponent::Serialize() const
    {
        std::vector<Property> properties;
        properties.reserve(Fields().size());
        for (const auto &field : Fields())
            properties.push_back({field.name, field.type, field.write(m_profile)});
        return properties;
    }

    void WaterEffectsComponent::Deserialize(const std::vector<Property> &properties)
    {
        // Missing properties keep their current values (defaults on a new component), so profiles saved before a
        // field existed load unchanged and partial lists (prefab overrides) touch only what they name.
        water::WaterEffectsProfile profile = m_profile;
        for (const auto &property : properties)
            for (const auto &field : Fields())
                if (field.name == property.name)
                {
                    field.read(profile, property.Text());
                    break;
                }
        SetProfile(profile);
    }

    void WaterEffectsComponent::SetProfile(const water::WaterEffectsProfile &profile)
    {
        m_profile = profile;
        water::SanitizeWaterEffectsProfile(m_profile);
        m_ripples.SetSettings(m_profile.ripples);
        if (!m_profile.ripples.enabled)
            m_ripples.Clear();
    }

    bool WaterEffectsComponent::IsAssetPathProperty(std::string_view name)
    {
        return name.ends_with(".Particles") || name.ends_with(".HeavyParticles") || name.ends_with(".Sound") ||
               name.ends_with(".HeavySound");
    }
}
