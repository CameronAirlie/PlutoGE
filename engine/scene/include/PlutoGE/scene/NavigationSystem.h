#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace PlutoGE::scene {
class Scene;
struct NavigationBakeSettings { glm::vec3 boundsMin{-50,-10,-50}; glm::vec3 boundsMax{50,20,50}; float cellSize=.5f; float maxSlopeDegrees=45; float maxStepHeight=.5f; bool staticObjectsOnly=false; std::vector<std::string> includeTags, excludeTags; };
struct NavigationPath { std::vector<glm::vec3> points; bool complete=false; };
class NavigationSystem {
public:
 bool Bake(const Scene&, const NavigationBakeSettings&); void Clear();
 NavigationPath FindPath(const glm::vec3&, const glm::vec3&, float agentRadius=0.0f, float agentHeight=0.0f) const; bool ProjectPoint(const glm::vec3&, glm::vec3&, float agentRadius=0.0f, float agentHeight=0.0f) const;
 bool IsBaked() const{return !m_debugPoints.empty();} const NavigationBakeSettings& GetSettings()const{return m_settings;}
 int GetWidth()const{return m_width;} int GetDepth()const{return m_depth;} const std::vector<glm::vec3>& GetDebugWalkablePoints()const{return m_debugPoints;}
 // Walkable surfaces stacked in one grid column (floors of a building, a bridge over a road).
 int GetLayerCount(int column)const{return column<0||column+1>=static_cast<int>(m_columnStart.size())?0:m_columnStart[column+1]-m_columnStart[column];}
 static constexpr int MaxLayersPerColumn=16;
private:
 // A layered heightfield: each grid column holds every surface found down through it, highest first.
 // Cells are indexed across all columns; neighbours link to the layer nearest in height.
 struct Cell{float height=0;float clearance=0;int column=0;bool walkable=false;}; int FindNearestCell(const glm::vec3&,float,float)const; glm::vec3 CellPosition(int)const; int FindLayer(int column,float height,float maxDelta)const; bool ComputeCellWalkableForAgent(int,float,float)const; bool IsCellWalkableForAgent(int,float,float)const; const std::vector<std::uint8_t>& GetAgentWalkability(float,float)const; bool IsSegmentWalkable(const glm::vec3&,const glm::vec3&,float,float)const;
 NavigationBakeSettings m_settings; int m_width=0,m_depth=0; std::vector<Cell>m_cells; std::vector<int>m_columnStart; std::vector<glm::vec3>m_debugPoints; mutable std::unordered_map<std::uint64_t,std::vector<std::uint8_t>>m_agentWalkabilityCache;
}; }
