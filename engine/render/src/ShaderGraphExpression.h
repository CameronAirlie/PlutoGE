#pragma once
#include "PlutoGE/render/ShaderGraph.h"
#include <charconv>
#include <cctype>
#include <stdexcept>

namespace PlutoGE::render::detail
{
    // Lower the common GLSL/HLSL expression syntax to the portable graph IR.
    class GraphExpressionParser
    {
        using K=ShaderGraphNodeKind;
        using Value=std::pair<int,std::string>;
        std::string_view text;
        size_t at=0;
        ShaderGraph graph;
        void Space(){while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;}
        bool Take(char c){Space();if(at<text.size()&&text[at]==c){++at;return true;}return false;}
        [[noreturn]] void Fail(){throw std::runtime_error("Invalid shader expression near character "+std::to_string(at+1));}
        Value Node(K kind,std::initializer_list<std::pair<const char *,Value>> inputs={},float number=0)
        {
            if(graph.nodes.size()>=64)throw std::runtime_error("Shader expression is too large.");
            const int id=int(graph.nodes.size()+1);
            graph.nodes.push_back({.id=id,.kind=kind,.value=glm::vec4(number)});
            for(const auto &[pin,value]:inputs)graph.links.push_back({int(graph.links.size()+1),value.first,value.second,id,pin});
            return {id,std::string(ShaderGraphOutputPins(graph.nodes.back()).front())};
        }
        Value Primary()
        {
            Space();
            if(Take('-'))return Node(K::Subtract,{{"A",Node(K::Float)},{"B",Primary()}});
            if(Take('+'))return Primary();
            if(Take('(')){auto value=Expression();if(!Take(')'))Fail();return value;}
            if(at>=text.size())Fail();
            if(std::isdigit(static_cast<unsigned char>(text[at]))||text[at]=='.'){
                float value=0;const auto parsed=std::from_chars(text.data()+at,text.data()+text.size(),value);
                if(parsed.ec!=std::errc{})Fail();at=size_t(parsed.ptr-text.data());if(at<text.size()&&text[at]=='f')++at;
                return Node(K::Float,{},value);
            }
            const auto begin=at;
            while(at<text.size()&&(std::isalnum(static_cast<unsigned char>(text[at]))||text[at]=='_'))++at;
            if(begin==at)Fail();const std::string name(text.substr(begin,at-begin));
            if(name.size()==1&&name[0]>='A'&&name[0]<='D')return {name[0]-'A'+1,"Out"};
            if(!Take('('))Fail();
            std::vector<Value> args;
            if(!Take(')')){do{args.push_back(Expression());}while(Take(','));if(!Take(')'))Fail();}
            if(name=="sin"&&args.size()==1)return Node(K::Sine,{{"Value",args[0]}});
            if(name=="normalize"&&args.size()==1)return Node(K::Normalize,{{"Value",args[0]}});
            if(name=="pow"&&args.size()==2)return Node(K::Power,{{"A",args[0]},{"B",args[1]}});
            if(name=="dot"&&args.size()==2)return Node(K::Dot,{{"A",args[0]},{"B",args[1]}});
            if((name=="mix"||name=="lerp")&&args.size()==3)return Node(K::Lerp,{{"A",args[0]},{"B",args[1]},{"T",args[2]}});
            if(name=="clamp"&&args.size()==3)return Node(K::Clamp,{{"Value",args[0]},{"Min",args[1]},{"Max",args[2]}});
            if(name=="saturate"&&args.size()==1)return Node(K::Clamp,{{"Value",args[0]},{"Min",Node(K::Float)},{"Max",Node(K::Float,{},1)}});
            int size=name=="vec2"||name=="float2"?2:name=="vec3"||name=="float3"?3:name=="vec4"||name=="float4"?4:0;
            if(size&&args.size()==1)return Node(size==2?K::Vec2:size==3?K::Vec3:K::Color,{{size==2?"Vec2":size==3?"Vec3":"Color",args[0]}});
            if(size&&args.size()==size){
                auto value=Node(size==2?K::Vec2:size==3?K::Vec3:K::Color);graph.nodes.back().componentPins=true;
                for(int i=0;i<size;++i)graph.links.push_back({int(graph.links.size()+1),args[i].first,args[i].second,value.first,std::string(1,(size==4?"RGBA":"XYZ")[i])});
                return value;
            }
            Fail();
        }
        Value Product(){auto value=Primary();for(;;){if(Take('*'))value=Node(K::Multiply,{{"A",value},{"B",Primary()}});else if(Take('/'))value=Node(K::Divide,{{"A",value},{"B",Primary()}});else return value;}}
        Value Expression(){auto value=Product();for(;;){if(Take('+'))value=Node(K::Add,{{"A",value},{"B",Product()}});else if(Take('-'))value=Node(K::Subtract,{{"A",value},{"B",Product()}});else return value;}}
    public:
        explicit GraphExpressionParser(std::string_view source):text(source){}
        ShaderGraph Parse()
        {
            if(text.size()>4096)Fail();
            for(char name='A';name<='D';++name){
                graph.variables.push_back({std::string(1,name),ShaderGraphValueType::Float,glm::vec4(0)});
                graph.nodes.push_back({.id=int(graph.nodes.size()+1),.kind=K::Parameter,.parameter=std::string(1,name)});
            }
            Space();if(text.substr(at,6)=="return")at+=6;
            const auto value=Expression();Take(';');Space();if(at!=text.size())Fail();
            const int output=int(graph.nodes.size()+1);graph.nodes.push_back({.id=output,.kind=K::Output});
            graph.links.push_back({int(graph.links.size()+1),value.first,value.second,output,"Albedo"});
            return graph;
        }
    };
}
