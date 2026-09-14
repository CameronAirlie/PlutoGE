#include "PlutoGE/render/ShaderGraph.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::render
{
    std::vector<std::string_view> ShaderGraphInputPins(const ShaderGraphNode &n)
    {
        using K = ShaderGraphNodeKind;
        switch (n.kind)
        {
        case K::Vec2: return n.componentPins ? std::vector<std::string_view>{"X","Y"} : std::vector<std::string_view>{"Vec2"};
        case K::Vec3: return n.componentPins ? std::vector<std::string_view>{"X","Y","Z"} : std::vector<std::string_view>{"Vec3"};
        case K::Color: return n.componentPins ? std::vector<std::string_view>{"R","G","B","A"} : std::vector<std::string_view>{"Color"};
        case K::Add: case K::Subtract: case K::Multiply: case K::Divide: case K::Dot: case K::Power: return {"A","B"};
        case K::Lerp: return {"A","B","T"};
        case K::Clamp: return {"Value","Min","Max"};
        case K::Normalize: case K::Sine: case K::OneMinus: return {"Value"};
        case K::NoiseTexture: return {"UV","Scale","Strength"};
        case K::TextureSample: return {"UV"};
        case K::Output: return {"Albedo","Normal","Metallic","Roughness","Opacity","Emission"};
        default: return {};
        }
    }
    std::vector<std::string_view> ShaderGraphOutputPins(const ShaderGraphNode &n)
    {
        using K = ShaderGraphNodeKind;
        switch(n.kind)
        {
        case K::Vec2: return {"Vec2","X","Y"};
        case K::Vec3: return {"Vec3","X","Y","Z"};
        case K::Color: return {"Color","R","G","B","A"};
        case K::NoiseTexture: return {"Value","Color"};
        case K::TextureSample: return {"Color","R","G","B","A"};
        case K::Output: return {};
        default: return {"Out"};
        }
    }
    namespace
    {
        struct Value { int reg; int dimensions; };
        struct Compiler
        {
            const ShaderGraph &graph;
            std::span<const ShaderGraphVariable> overrides;
            ShaderGraphProgram program;
            std::unordered_map<int, const ShaderGraphNode *> nodes;
            std::unordered_map<std::string, const ShaderGraphLink *> links;
            std::unordered_map<std::string, Value> compiled;
            std::unordered_map<int, Value> inputs;
            std::unordered_map<int, int> visited;
            static std::string Key(int id, std::string_view pin) { return std::to_string(id) + ":" + std::string(pin); }
            static void Require(bool condition, const std::string &message) { if (!condition) throw std::runtime_error(message); }
            static bool Finite(glm::vec4 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w); }
            void Visit(int id)
            {
                Require(visited[id] != 1, "Cycle at node " + std::to_string(id) + ".");
                if (visited[id] == 2) return;
                visited[id] = 1;
                for (const auto &l : graph.links) if (l.toNodeId == id) Visit(l.fromNodeId);
                visited[id] = 2;
            }
            const ShaderGraphVariable &Variable(const ShaderGraphNode &node)
            {
                const auto v = std::find_if(graph.variables.begin(), graph.variables.end(), [&](const auto &v) { return v.name == node.parameter; });
                Require(v != graph.variables.end(), "Node " + std::to_string(node.id) + " references missing parameter '" + node.parameter + "'.");
                return *v;
            }
            int Check()
            {
                Require(!graph.nodes.empty() && graph.nodes.size() <= 256, "Graph must contain 1 to 256 nodes.");
                Require(graph.links.size() <= 1024, "Graph has too many links.");
                Require(std::isfinite(graph.outline.width) && graph.outline.width >= 0 && Finite(glm::vec4(graph.outline.color,1)), "Outline values must be finite and width non-negative.");
                std::unordered_set<std::string> names;
                for (const auto &v : graph.variables)
                {
                    Require(!v.name.empty() && v.name.find_first_of("|\r\n") == std::string::npos && names.insert(v.name).second, "Parameter names must be non-empty and unique, without | or newlines.");
                    Require(int(v.type) >= 0 && int(v.type) <= 3 && Finite(v.value), "Invalid parameter type or default: " + v.name);
                }
                names.clear();
                for (const auto &v : overrides)
                {
                    Require(names.insert(v.name).second && Finite(v.value), "Invalid or duplicate material parameter: " + v.name);
                    // Stale overrides are preserved for switching shader assets, but never applied by position.
                    const auto current = std::find_if(graph.variables.begin(), graph.variables.end(), [&](const auto &p) { return p.name == v.name; });
                    if (current != graph.variables.end()) Require(v.type == current->type, "Material parameter type changed: " + v.name + ". Reset its override.");
                }
                int output = 0;
                for (const auto &n : graph.nodes)
                {
                    Require(n.id > 0 && nodes.emplace(n.id, &n).second, "Node IDs must be positive and unique.");
                    Require(int(n.kind) >= 0 && int(n.kind) <= int(ShaderGraphNodeKind::TextureSample), "Unknown node kind at node " + std::to_string(n.id));
                    Require(Finite(n.value), "Non-finite value at node " + std::to_string(n.id));
                    Require(n.name.find_first_of("|\r\n") == std::string::npos, "Node names cannot contain | or newlines.");
                    if (n.kind == ShaderGraphNodeKind::Output) { Require(output == 0, "Graph must have exactly one Output node."); output = n.id; }
                    if (n.kind == ShaderGraphNodeKind::MaterialInput) Require(int(n.materialInput) >= 0 && int(n.materialInput) <= 6, "Unknown material input.");
                    if (n.kind == ShaderGraphNodeKind::TextureSample) Require(int(n.materialInput) >= 0 && int(n.materialInput) <= 3, "Texture Sample requires Color, Normal, Metallic or Roughness.");
                    if (n.kind == ShaderGraphNodeKind::Parameter) Variable(n);
                }
                Require(output != 0, "Graph has no Output node.");
                std::unordered_set<int> ids;
                for (const auto &l : graph.links)
                {
                    Require(l.id > 0 && ids.insert(l.id).second, "Link IDs must be positive and unique.");
                    Require(nodes.contains(l.fromNodeId) && nodes.contains(l.toNodeId), "Link references a missing node.");
                    const auto from = ShaderGraphOutputPins(*nodes.at(l.fromNodeId));
                    const auto to = ShaderGraphInputPins(*nodes.at(l.toNodeId));
                    Require(std::find(from.begin(), from.end(), l.fromPin) != from.end(), "Unknown source pin: " + l.fromPin);
                    Require(std::find(to.begin(), to.end(), l.toPin) != to.end(), "Unknown destination pin: " + l.toPin);
                    Require(links.emplace(Key(l.toNodeId,l.toPin), &l).second, "An input pin has more than one connection.");
                }
                for (const auto &n : graph.nodes) Visit(n.id);
                return output;
            }
            Value Emit(int op, int a, int b, int c, int dimensions, glm::vec4 value = {})
            {
                int &count = program.data.header.x;
                Require(count < kMaxShaderGraphInstructions, "Graph exceeds 64 evaluated operations. Simplify the connected graph.");
                int reg = count++;
                program.data.instructions[reg] = {op,a,b,c}; program.data.values[reg] = value;
                return {reg, dimensions};
            }
            Value Constant(glm::vec4 v, int dim)
            {
                if (dim == 1) v = glm::vec4(v.x);
                else if (dim == 2) v = glm::vec4(v.x,v.y,0,1);
                else if (dim == 3) v.w = 1;
                for (int i=0;i<program.data.header.x;++i)
                    if (program.data.instructions[i].x == 0 && program.data.values[i] == v) return {i,dim};
                return Emit(0,0,0,0,dim,v);
            }
            Value Input(int kind)
            {
                if (inputs.contains(kind)) return inputs.at(kind);
                constexpr int dims[]{4,3,1,1,1,2,3,1,3,3,3};
                return inputs[kind] = Emit(1,kind,0,0,dims[kind]);
            }
            Value In(const ShaderGraphNode &n, std::string_view pin, Value fallback)
            {
                const auto it = links.find(Key(n.id,pin));
                return it == links.end() ? fallback : Node(*nodes.at(it->second->fromNodeId), it->second->fromPin);
            }
            int Promote(Value a, Value b)
            {
                Require(a.dimensions == b.dimensions || a.dimensions == 1 || b.dimensions == 1, "Math inputs must have matching vector sizes (scalars broadcast).");
                return std::max(a.dimensions,b.dimensions);
            }
            Value Node(const ShaderGraphNode &n, std::string_view pin)
            {
                const auto key = Key(n.id,pin);
                if (compiled.contains(key)) return compiled.at(key);
                using K = ShaderGraphNodeKind;
                Value result{};
                const auto scalar = [&](float v) { return Constant(glm::vec4(v),1); };
                switch(n.kind)
                {
                case K::MaterialInput: result = Input(int(n.materialInput)); break;
                case K::MeshUV: result = Input(5); break;
                case K::Time: result = Input(7); break;
                case K::WorldPosition: result = Input(8); break;
                case K::WorldNormal: result = Input(9); break;
                case K::ViewDirection: result = Input(10); break;
                case K::Parameter:
                {
                    const auto &v = Variable(n);
                    auto value = v.value;
                    for (const auto &o : overrides) if (o.name == v.name) value = o.value;
                    const int dim=int(v.type)+1;
                    if(dim==1) value=glm::vec4(value.x);
                    else if(dim==2) value=glm::vec4(value.x,value.y,0,1);
                    else if(dim==3) value.w=1;
                    result = Emit(17,0,0,0,dim,value); break;
                }
                case K::Float: result = scalar(n.value.x); break;
                case K::Vec2: case K::Vec3: case K::Color:
                {
                    const int dim = n.kind == K::Vec2 ? 2 : n.kind == K::Vec3 ? 3 : 4;
                    const std::string_view packed = dim==2 ? "Vec2" : dim==3 ? "Vec3" : "Color";
                    const std::string_view components = dim==4 ? "RGBA" : "XYZ";
                    if (n.componentPins)
                    {
                        const auto component = [&](int i)
                        {
                            auto v=In(n,std::string(1,components[i]),scalar(n.value[i]));
                            Require(v.dimensions==1,"Component pins require scalar values.");
                            return v;
                        };
                        if(pin!=packed) result=component(int(components.find(pin)));
                        else
                        {
                            auto x=component(0), y=component(1), z=dim>=3?component(2):scalar(0), w=dim==4?component(3):scalar(1);
                            result=Emit(18,x.reg,y.reg,z.reg,dim,glm::vec4(float(w.reg),0,0,0));
                        }
                    }
                    else
                    {
                        auto v=In(n,packed,Constant(n.value,dim));
                        Require(v.dimensions==dim || v.dimensions==1,"Packed vector pin has incompatible size.");
                        result=pin==packed ? v : Emit(15,v.reg,int(components.find(pin)),0,1);
                    }
                    break;
                }
                case K::Add: case K::Subtract: case K::Multiply: case K::Divide: case K::Dot: case K::Power:
                {
                    float def = n.kind == K::Multiply || n.kind == K::Divide || n.kind == K::Power ? 1.0f : 0.0f;
                    auto a=In(n,"A",scalar(def)), b=In(n,"B",scalar(def));
                    int dim=Promote(a,b);
                    int op = n.kind == K::Dot ? 10 : n.kind == K::Power ? 12 : int(n.kind)-3;
                    result=Emit(op,a.reg,b.reg,dim,n.kind==K::Dot?1:dim); break;
                }
                case K::Lerp: case K::Clamp:
                {
                    bool lerp=n.kind==K::Lerp;
                    auto a=In(n,lerp?"A":"Value",scalar(0)), b=In(n,lerp?"B":"Min",scalar(lerp?1:0)), c=In(n,lerp?"T":"Max",scalar(lerp?.5f:1));
                    int dim=Promote(a,b); dim=Promote({a.reg,dim},c);
                    result=Emit(lerp?6:7,a.reg,b.reg,c.reg,dim);break;
                }
                case K::Normalize: case K::Sine: case K::OneMinus:
                {
                    auto v=In(n,"Value",n.kind==K::Normalize?Input(1):scalar(0));
                    result=Emit(n.kind==K::Normalize?8:n.kind==K::Sine?11:13,v.reg,v.dimensions,0,v.dimensions);break;
                }
                case K::NoiseTexture:
                {
                    auto uv=In(n,"UV",Input(5)), scale=In(n,"Scale",scalar(std::max(n.value.x,0.0f))), strength=In(n,"Strength",scalar(std::max(n.value.y,0.0f)));
                    Require(uv.dimensions==2 && scale.dimensions==1 && strength.dimensions==1, "Noise requires Vec2 UV and scalar Scale/Strength.");
                    result=Emit(9,uv.reg,scale.reg,strength.reg,1);
                    if (pin=="Color") result=Emit(16,result.reg,0,0,4);
                    break;
                }
                case K::TextureSample:
                {
                    auto uv=In(n,"UV",Input(5)); Require(uv.dimensions==2,"Texture UV requires Vec2.");
                    auto packed=Emit(14,uv.reg,int(n.materialInput),0,4);
                    result=packed;
                    if (pin!="Color") result=Emit(15,packed.reg,int(std::string_view("RGBA").find(pin)),0,1);
                    break;
                }
                default: throw std::runtime_error("Invalid value node.");
                }
                return compiled[key]=result;
            }
            void Run()
            {
                const auto &out=*nodes.at(Check());
                constexpr const char *pins[]{"Albedo","Normal","Metallic","Roughness","Opacity","Emission"};
                constexpr int kinds[]{0,1,2,3,4,6};
                for(int i=0;i<6;++i)
                {
                    auto value=In(out,pins[i],Input(kinds[i]));
                    (i<4?program.data.outputs0[i]:program.data.outputs1[i-4])=value.reg;
                }
                program.data.header.y=graph.unlit?1:0;
                std::uint64_t h=1469598103934665603ull;
                // Program is fixed-layout, fully initialized and contains no pointers or padding.
                for(auto b:std::as_bytes(std::span(&program.data,1))) { h^=std::to_integer<unsigned char>(b);h*=1099511628211ull; }
                program.hash=h;
            }
        };
    }
    std::shared_ptr<const ShaderGraphProgram> BuildShaderGraphProgram(const ShaderGraph &graph, std::span<const ShaderGraphVariable> overrides, std::string *error)
    {
        if(error) error->clear();
        try { Compiler compiler{graph,overrides}; compiler.Run(); return std::make_shared<ShaderGraphProgram>(std::move(compiler.program)); }
        catch(const std::exception &e) { if(error) *error=e.what(); return {}; }
    }
    bool ValidateShaderGraph(const ShaderGraph &graph, std::string *error) { return bool(BuildShaderGraphProgram(graph,{},error)); }
    float ShaderGraphTimeSeconds()
    {
        static const auto start=std::chrono::steady_clock::now();
        return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
    }
}
