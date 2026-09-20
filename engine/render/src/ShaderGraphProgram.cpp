#include "PlutoGE/render/ShaderGraph.h"
#include "ShaderGraphExpression.h"
#include <algorithm>
#include <atomic>
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
        case K::Step: return {"Edge","Value"};
        case K::Smoothstep: return {"Min","Max","Value"};
        case K::Floor: return {"Value"};
        case K::Expression: case K::Subgraph: return {"A","B","C","D"};
        case K::Vec2: return n.componentPins ? std::vector<std::string_view>{"X","Y"} : std::vector<std::string_view>{"Vec2"};
        case K::Vec3: return n.componentPins ? std::vector<std::string_view>{"X","Y","Z"} : std::vector<std::string_view>{"Vec3"};
        case K::Color: return n.componentPins ? std::vector<std::string_view>{"R","G","B","A"} : std::vector<std::string_view>{"Color"};
        case K::Add: case K::Subtract: case K::Multiply: case K::Divide: case K::Dot: case K::Power: return {"A","B"};
        case K::Lerp: return {"A","B","T"};
        case K::Clamp: return {"Value","Min","Max"};
        case K::Normalize: case K::Sine: case K::OneMinus: return {"Value"};
        case K::NoiseTexture: return {"UV","Scale","Strength"};
        case K::TextureSample: case K::SceneColor: case K::SceneDepth: return {"UV"};
        case K::Output: return {"Albedo","Normal","Metallic","Roughness","Opacity","Emission","Vertex Offset","Direct Lighting"};
        default: return {};
        }
    }
    std::vector<std::string_view> ShaderGraphOutputPins(const ShaderGraphNode &n)
    {
        using K = ShaderGraphNodeKind;
        switch(n.kind)
        {
        case K::Subgraph: return {"Albedo","Normal","Metallic","Roughness","Opacity","Emission","Vertex Offset","Direct Lighting"};
        case K::Vec2: return {"Vec2","X","Y"};
        case K::Vec3: return {"Vec3","X","Y","Z"};
        case K::Color: return {"Color","R","G","B","A"};
        case K::NoiseTexture: return {"Value","Color"};
        case K::TextureSample: case K::SceneColor: return {"Color","R","G","B","A"};
        case K::Output: return {};
        default: return {"Out"};
        }
    }
    namespace
    {
        ShaderGraph FlattenGraph(const ShaderGraph &source, int depth=0)
        {
            if(depth>8)throw std::runtime_error("Subgraphs exceed eight nesting levels or contain a recursive reference.");
            if(source.nodes.size()>256||source.links.size()>1024)throw std::runtime_error("Graph exceeds node or link limits.");
            std::unordered_map<int,const ShaderGraphNode*> sourceNodes;
            std::unordered_set<std::string> variableNames;
            for (const auto &variable : source.variables)
                if (variable.name.empty() || !variableNames.insert(variable.name).second ||
                    variable.name.find_first_of("|\r\n") != std::string::npos || int(variable.type) < 0 || int(variable.type) > 3)
                    throw std::runtime_error("Invalid or duplicate graph variable.");
            for (const auto &node : source.nodes)
                if (node.parameter.find_first_of("|\r\n") != std::string::npos)
                    throw std::runtime_error("Node references and expressions must be single-line and cannot contain '|'.");
            for(const auto &n:source.nodes)if(n.id<=0||!sourceNodes.emplace(n.id,&n).second)throw std::runtime_error("Node IDs must be positive and unique.");
            std::unordered_set<int> linkIds;std::unordered_set<std::string> inputPins;
            for(const auto &l:source.links){
                if(l.id<=0||!linkIds.insert(l.id).second||!sourceNodes.contains(l.fromNodeId)||!sourceNodes.contains(l.toNodeId))throw std::runtime_error("Invalid graph link.");
                const auto from=ShaderGraphOutputPins(*sourceNodes.at(l.fromNodeId)),to=ShaderGraphInputPins(*sourceNodes.at(l.toNodeId));
                if(std::find(from.begin(),from.end(),l.fromPin)==from.end()||std::find(to.begin(),to.end(),l.toPin)==to.end()||
                    !inputPins.insert(std::to_string(l.toNodeId)+":"+l.toPin).second)throw std::runtime_error("Invalid or duplicate input connection.");
            }
            std::unordered_map<int,int> visited;
            std::function<void(int)> visit=[&](int id){if(visited[id]==1)throw std::runtime_error("Graph contains a cycle.");if(visited[id]==2)return;visited[id]=1;
                for(const auto &l:source.links)if(l.toNodeId==id)visit(l.fromNodeId);visited[id]=2;};
            for(const auto &n:source.nodes)visit(n.id);
            ShaderGraph result=source;
            int next=1;
            for(const auto &n:result.nodes)next=std::max(next,n.id+1);
            for(size_t index=0;index<result.nodes.size();)
            {
                auto call=result.nodes[index];
                if(call.kind==ShaderGraphNodeKind::Expression){
                    call.subgraph=std::make_shared<const ShaderGraph>(detail::GraphExpressionParser(call.parameter).Parse());
                    call.kind=ShaderGraphNodeKind::Subgraph;
                    for(auto &link:result.links)if(link.fromNodeId==call.id&&link.fromPin=="Out")link.fromPin="Albedo";
                }
                if(call.kind!=ShaderGraphNodeKind::Subgraph){++index;continue;}
                if(!call.subgraph)throw std::runtime_error("Unresolved subgraph: "+call.parameter);
                const auto child=FlattenGraph(*call.subgraph,depth+1);
                if(child.variables.size()>4)throw std::runtime_error("Subgraphs expose at most four parameters (A-D).");
                const ShaderGraphNode *output=nullptr;
                for(const auto &n:child.nodes)if(n.kind==ShaderGraphNodeKind::Output){
                    if(output)throw std::runtime_error("Subgraph has multiple outputs: "+call.parameter);
                    output=&n;
                }
                if(!output)throw std::runtime_error("Subgraph has no output: "+call.parameter);
                using Endpoint=std::pair<int,std::string>;
                std::unordered_map<int,int> ids;
                std::unordered_map<int,Endpoint> parameters;
                std::vector<ShaderGraphNode> copied;
                for(auto texture:child.textures){texture.name=std::to_string(call.id)+"/"+texture.name;result.textures.push_back(std::move(texture));}
                for(auto n:child.nodes)
                {
                    if(n.kind==ShaderGraphNodeKind::TextureSample&&!n.parameter.empty())n.parameter=std::to_string(call.id)+"/"+n.parameter;
                    if(n.kind==ShaderGraphNodeKind::Output)continue;
                    const int old=n.id;n.id=next++;ids[old]=n.id;
                    if(n.kind==ShaderGraphNodeKind::Parameter)
                    {
                        const auto parameter=std::find_if(child.variables.begin(),child.variables.end(),[&](const auto &v){return v.name==n.parameter;});
                        if(parameter==child.variables.end())throw std::runtime_error("Missing subgraph parameter.");
                        const std::string inputPin(1,char('A'+std::distance(child.variables.begin(),parameter)));
                        const auto input=std::find_if(result.links.begin(),result.links.end(),[&](const auto &l){return l.toNodeId==call.id&&l.toPin==inputPin;});
                        if(input!=result.links.end()){parameters[old]={input->fromNodeId,input->fromPin};continue;}
                        const auto variable=std::find_if(child.variables.begin(),child.variables.end(),[&](const auto &v){return v.name==n.parameter;});
                        if(variable==child.variables.end())throw std::runtime_error("Missing subgraph parameter: "+n.parameter);
                        constexpr ShaderGraphNodeKind kinds[]{ShaderGraphNodeKind::Float,ShaderGraphNodeKind::Vec2,ShaderGraphNodeKind::Vec3,ShaderGraphNodeKind::Color};
                        if(int(variable->type)<0 || int(variable->type)>3)throw std::runtime_error("Invalid subgraph parameter type.");
                        n.kind=kinds[int(variable->type)];n.value=variable->value;n.parameter.clear();n.componentPins=false;
                        parameters[old]={n.id,std::string(ShaderGraphOutputPins(n).front())};
                    }
                    copied.push_back(std::move(n));
                }
                const auto endpoint=[&](int id,const std::string &pin)->Endpoint {
                    if(parameters.contains(id))return parameters.at(id);
                    if(!ids.contains(id))throw std::runtime_error("Invalid link inside subgraph.");
                    return {ids.at(id),pin};
                };
                std::vector<ShaderGraphLink> links;
                for(auto l:child.links)if(l.toNodeId!=output->id){
                    const auto from=endpoint(l.fromNodeId,l.fromPin);
                    l.fromNodeId=from.first;l.fromPin=from.second;
                    if(!ids.contains(l.toNodeId))throw std::runtime_error("Invalid subgraph destination.");
                    l.toNodeId=ids.at(l.toNodeId);links.push_back(std::move(l));
                }
                for(auto l:result.links)
                {
                    if(l.toNodeId==call.id)continue;
                    if(l.fromNodeId==call.id)
                    {
                        const auto wire=std::find_if(child.links.begin(),child.links.end(),[&](const auto &c){return c.toNodeId==output->id&&c.toPin==l.fromPin;});
                        if(wire!=child.links.end()){
                            const auto from=endpoint(wire->fromNodeId,wire->fromPin);l.fromNodeId=from.first;l.fromPin=from.second;
                        }else{
                            ShaderGraphNode fallback;fallback.id=next++;fallback.kind=ShaderGraphNodeKind::MaterialInput;
                            constexpr const char *pins[]{"Albedo","Normal","Metallic","Roughness","Opacity","Emission","Vertex Offset","Direct Lighting"};
                            int kind=0;while(kind<8&&l.fromPin!=pins[kind])++kind;
                            if(kind==8)throw std::runtime_error("Invalid subgraph output pin.");
                            constexpr int inputs[]{0,1,2,3,4,6};
                            if(kind>=6){fallback.kind=ShaderGraphNodeKind::Vec3;fallback.value=glm::vec4(0);l.fromPin="Vec3";}
                            else {fallback.materialInput=ShaderGraphMaterialInput(inputs[kind]);l.fromPin="Out";}
                            l.fromNodeId=fallback.id;copied.push_back(fallback);
                        }
                    }
                    links.push_back(std::move(l));
                }
                result.nodes.erase(result.nodes.begin()+index);
                result.nodes.insert(result.nodes.end(),copied.begin(),copied.end());
                result.links=std::move(links);
                for(size_t i=0;i<result.links.size();++i)result.links[i].id=int(i+1);
                if(result.nodes.size()>256||result.links.size()>1024)throw std::runtime_error("Expanded subgraph exceeds graph limits.");
            }
            return result;
        }
        struct Value { int reg; int dimensions; };
        struct Compiler
        {
            const ShaderGraph &graph;
            std::span<const ShaderGraphVariable> overrides;
            ShaderGraphProgram program;
            bool lightingStage = false;
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
                Require(graph.tessellation>=0&&graph.tessellation<=3,"Tessellation must be between zero and three.");
                Require(!graph.nodes.empty() && graph.nodes.size() <= 256, "Graph must contain 1 to 256 nodes.");
                Require(graph.links.size() <= 1024, "Graph has too many links.");
                Require(std::isfinite(graph.outline.width) && graph.outline.width >= 0 && Finite(glm::vec4(graph.outline.color,1)), "Outline values must be finite and width non-negative.");
                std::unordered_set<std::string> names;
                Require(graph.passes.size()<=4,"A material can have at most four additional passes.");
                for(const auto &pass:graph.passes)Require(!pass.empty()&&pass.find_first_of("|\r\n")==std::string::npos,"Invalid pass reference.");
                Require(graph.textures.size()<=4,"Graphs support up to four additional texture parameters.");
                for(const auto &t:graph.textures)
                    Require(!t.name.empty() && t.name.find_first_of("|\r\n")==std::string::npos && names.insert(t.name).second &&
                        t.reference.find_first_of("|\r\n")==std::string::npos,"Invalid or duplicate texture parameter.");
                names.clear();
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
                    Require(int(n.kind) >= 0 && int(n.kind) <= int(ShaderGraphNodeKind::ShadowAttenuation), "Unknown node kind at node " + std::to_string(n.id));
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
                constexpr int dims[]{4,3,1,1,1,2,3,1,3,3,3,2,3,3,1,1};
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
                case K::LightDirection: case K::LightColor: case K::LightAttenuation: case K::ShadowAttenuation:
                    Require(lightingStage,"Light inputs can only connect to Direct Lighting, not surface or vertex outputs.");
                    result=Input(12+int(n.kind)-int(K::LightDirection)); break;
                case K::Step:
                {
                    auto edge=In(n,"Edge",scalar(.5f)), value=In(n,"Value",scalar(0));
                    result=Emit(19,edge.reg,value.reg,0,Promote(edge,value));break;
                }
                case K::Floor:
                {
                    auto value=In(n,"Value",scalar(0));
                    result=Emit(20,value.reg,0,0,value.dimensions);break;
                }
                case K::Smoothstep:
                {
                    auto low=In(n,"Min",scalar(0)), high=In(n,"Max",scalar(1)), value=In(n,"Value",scalar(0));
                    int dim=Promote(low,high);dim=Promote({low.reg,dim},value);
                    result=Emit(21,low.reg,high.reg,value.reg,dim);break;
                }
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
                        result=pin==packed ? Value{v.reg,dim} : Emit(15,v.reg,int(components.find(pin)),0,1);
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
                case K::ScreenUV: result=Input(11);break;
                case K::SceneColor: case K::SceneDepth:
                {
                    auto uv=In(n,"UV",Input(11));Require(uv.dimensions==2,"Scene sampling requires Vec2 UV.");
                    result=Emit(14,uv.reg,n.kind==K::SceneColor?8:9,0,4);
                    if(n.kind==K::SceneDepth)result=Emit(15,result.reg,0,0,1);
                    else if(pin!="Color")result=Emit(15,result.reg,int(std::string_view("RGBA").find(pin)),0,1);
                    break;
                }
                case K::TextureSample:
                {
                    auto uv=In(n,"UV",Input(5)); Require(uv.dimensions==2,"Texture UV requires Vec2.");
                    int slot=int(n.materialInput);
                    if(!n.parameter.empty()) {
                        auto texture=std::find_if(graph.textures.begin(),graph.textures.end(),[&](const auto &t){return t.name==n.parameter;});
                        Require(texture!=graph.textures.end(),"Missing texture parameter: "+n.parameter);
                        slot=4+int(std::distance(graph.textures.begin(),texture));
                    }
                    auto packed=Emit(14,uv.reg,slot,0,4);
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
                if (links.contains(Key(out.id,"Vertex Offset")))
                {
                    auto offset = Node(*nodes.at(links.at(Key(out.id,"Vertex Offset"))->fromNodeId),
                        links.at(Key(out.id,"Vertex Offset"))->fromPin);
                    Require(offset.dimensions == 3, "Vertex Offset requires a world-space Vec3.");
                    program.data.outputs1.z = offset.reg;
                    program.data.header.z = program.data.header.x;
                    for(int i=0;i<program.data.header.z;++i){const auto op=program.data.instructions[i];
                        Require(!(op.x==1&&op.y==11)&&!(op.x==14&&op.z>=8),"Scene reads are only available to the surface stage.");}
                }
                constexpr const char *pins[]{"Albedo","Normal","Metallic","Roughness","Opacity","Emission"};
                constexpr int kinds[]{0,1,2,3,4,6};
                for(int i=0;i<6;++i)
                {
                    auto value=In(out,pins[i],Input(kinds[i]));
                    (i<4?program.data.outputs0[i]:program.data.outputs1[i-4])=value.reg;
                }
                if(links.contains(Key(out.id,"Direct Lighting")))
                {
                    Require(!graph.unlit,"Direct Lighting requires a lit surface; disable Unlit surface.");
                    const int surfaceCount=program.data.header.x;
                    lightingStage=true;
                    auto value=In(out,"Direct Lighting",Constant(glm::vec4(0),3));
                    Require(value.dimensions==1 || value.dimensions==3 || value.dimensions==4,"Direct Lighting requires a scalar or RGB colour.");
                    program.data.outputs1.w=(surfaceCount<<8)|(value.reg+1);
                }
                for(int i=0;i<program.data.header.x;++i)if(program.data.instructions[i].x==14 && program.data.instructions[i].z>=8)program.requiresSceneTextures=true;
                for(int i=0;i<program.data.header.x;++i)if(program.data.instructions[i].x==1){
                    program.usesTime|=program.data.instructions[i].y==7;program.usesViewDirection|=program.data.instructions[i].y==10;}
                program.textures=graph.textures;
                program.data.header.y=graph.unlit?1:0;
                program.data.header.w=graph.tessellation;
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
        try { auto expanded=FlattenGraph(graph); Compiler compiler{expanded,overrides}; compiler.Run(); return std::make_shared<ShaderGraphProgram>(std::move(compiler.program)); }
        catch(const std::exception &e) { if(error) *error=e.what(); return {}; }
    }
    bool ValidateShaderGraph(const ShaderGraph &graph, std::string *error) { return bool(BuildShaderGraphProgram(graph,{},error)); }
    namespace { std::atomic<float> graphClock{-1.0f},previousGraphClock{-1.0f}; }
    void SetShaderGraphTimeSeconds(float seconds) { if(std::isfinite(seconds) && seconds>=0){const auto previous=graphClock.exchange(seconds);previousGraphClock.store(previous<0?seconds:previous);} }
    float ShaderGraphPreviousTimeSeconds(){const float previous=previousGraphClock.load();return previous<0?ShaderGraphTimeSeconds():previous;}
    void ResetShaderGraphClock() { graphClock.store(-1.0f);previousGraphClock.store(-1.0f); }
    float ShaderGraphTimeSeconds()
    {
        const float supplied=graphClock.load();
        if(supplied>=0) return supplied;
        static const auto start=std::chrono::steady_clock::now();
        return std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
    }
}
