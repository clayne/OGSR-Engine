////////////////////////////////////////////////////////////////////////////
//	Module 		: game_graph_script.cpp
//	Created 	: 02.11.2005
//  Modified 	: 02.11.2005
//	Author		: Dmitriy Iassenev
//	Description : Game graph class script export
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "game_graph.h"
#include "ai_space.h"

using namespace luabind;

const CGameGraph* get_game_graph() { return (&ai().game_graph()); }

const CGameGraph::CHeader* get_header(const CGameGraph* self) { return (&self->header()); }

bool get_accessible1(const CGameGraph* self, const u32& vertex_id) { return (self->accessible(vertex_id)); }

void get_accessible2(const CGameGraph* self, const u32& vertex_id, bool value) { self->accessible(vertex_id, value); }

u32 vertex_count(const CGameGraph* self) { return self->header().vertex_count(); }

Fvector CVertex__level_point(const CGameGraph::CVertex* vertex)
{
    THROW(vertex);
    return (vertex->level_point());
}

Fvector CVertex__game_point(const CGameGraph::CVertex* vertex)
{
    THROW(vertex);
    return (vertex->game_point());
}

const CGameLevelCrossTable* get_cross_table() { return &ai().cross_table(); }

CGameLevelCrossTable* get_cross_table_for_level(LPCSTR level_name)
{
    const GameGraph::SLevel* level = ai().game_graph().header().level(level_name, true);
    if (!level)
    {
        Msg("! Unknown level {%}!", level_name);
        return nullptr;
    }

    u32 level_id = level->id();

    CGameLevelCrossTable* result = ai().game_graph().find_cross_table_for_level(level_id);

    if (result)
        return result;

    string_path fn;
    strconcat(sizeof(fn), fn, level_name, "\\", CROSS_TABLE_NAME);

    string_path fName;
    FS.update_path(fName, "$game_levels$", fn);
    return xr_new<CGameLevelCrossTable>(fName);
}

Fvector4 CVertex__mask_(const CGameGraph::CVertex* vertex)
{
    const u8* mask = vertex->vertex_type();
    return Fvector4{(float)mask[0], (float)mask[1], (float)mask[2], (float)mask[3]};
}

void CGameGraph::script_register(lua_State* L)
{
    module(L)[def("game_graph", &get_game_graph),

              class_<CGameGraph>("CGameGraph")
                  .def("accessible", &get_accessible1)
                  .def("accessible", &get_accessible2)
                  .def("valid_vertex_id", &CGameGraph::valid_vertex_id)
                  .def("vertex", &CGameGraph::vertex)
                  .def("vertex_id", &CGameGraph::vertex_id)
                  .def("vertex_count", &vertex_count),

              class_<CVertex>("GameGraph__CVertex")
                  .def("level_point", &CVertex__level_point)
                  .def("game_point", &CVertex__game_point)
                  .def("level_id", &CVertex::level_id)
                  .def("level_vertex_id", &CVertex::level_vertex_id)
                  .def("mask", &CVertex__mask_),

              def("cross_table", &get_cross_table),
              def("cross_table", &get_cross_table_for_level, adopt<result>()),

              class_<CGameLevelCrossTable>("CGameLevelCrossTable").def("vertex", &CGameLevelCrossTable::vertex),

              class_<CGameLevelCrossTable::CCell>("CGameLevelCrossTable__CCell").def("game_vertex_id", &CGameLevelCrossTable::CCell::game_vertex_id)];
}
