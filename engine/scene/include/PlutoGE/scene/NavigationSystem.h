#pragma once
#include <glm/glm.hpp>
#include <vector>
namespace PlutoGE::scene {
class Scene;
struct NavigationBakeSettings { glm::vec3 boundsMin{-50,-10,-50}; glm::vec3 boundsMax{50,20,50}; float cellSize=.5f; float agentHeight=1.8f; float agentRadius=1.2f; float maxSlopeDegrees=45; float maxStepHeight=.5f; };
struct NavigationPath { std::vector<glm::vec3> points; bool complete=false; };
class NavigationSystem {
public:
 bool Bake(const Scene&, const NavigationBakeSettings&); void Clear();
 NavigationPath FindPath(const glm::vec3&, const glm::vec3&) const; bool ProjectPoint(const glm::vec3&, glm::vec3&) const;
 bool IsBaked() const{return !m_cells.empty();} const NavigationBakeSettings& GetSettings()const{return m_settings;}
 int GetWidth()const{return m_width;} int GetDepth()const{return m_depth;} const std::vector<glm::vec3>& GetDebugWalkablePoints()const{return m_debugPoints;}
private:
 struct Cell{float height=0;bool walkable=false;}; int FindNearestCell(const glm::vec3&)const; glm::vec3 CellPosition(int)const; bool IsSegmentWalkable(const glm::vec3&,const glm::vec3&)const;
 NavigationBakeSettings m_settings; int m_width=0,m_depth=0; std::vector<Cell>m_cells; std::vector<glm::vec3>m_debugPoints;
}; }
