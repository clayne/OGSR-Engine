#include "stdafx.h"

#ifdef DX10_FLUID_ENABLE

#include "dx103DFluidGrid.h"

#include "../dx10BufferUtils.h"

#include <Utilities/FlexibleVertexFormat.h>

struct VS_INPUT_FLUIDSIM_STRUCT
{
    D3DXVECTOR3 Pos; // Clip space position for slice vertices
    D3DXVECTOR3 Tex; // Cell coordinates in 0-"texture dimension" range
};

namespace
{ //	namespace start

void ComputeRowColsForFlat3DTexture(int depth, int* outCols, int* outRows)
{
    // Compute # of m_iRows and m_iCols for a "flat 3D-texture" configuration
    // (in this configuration all the slices in the volume are spread in a single 2D texture)
    const int m_iRows = (int)floorf(_sqrt((float)depth));
    int m_iCols = m_iRows;
    while (m_iRows * m_iCols < depth)
    {
        m_iCols++;
    }
    VERIFY(m_iRows * m_iCols >= depth);

    *outCols = m_iCols;
    *outRows = m_iRows;
}

} // namespace

#define VERTICES_PER_SLICE 6
#define VERTICES_PER_LINE 2
#define LINES_PER_SLICE 4

dx103DFluidGrid::dx103DFluidGrid() {}

dx103DFluidGrid::~dx103DFluidGrid()
{
    _RELEASE(m_pRenderQuadBuffer);
    _RELEASE(m_pSlicesBuffer);
    _RELEASE(m_pBoundarySlicesBuffer);
    _RELEASE(m_pBoundaryLinesBuffer);
}

void dx103DFluidGrid::Initialize(int gridWidth, int gridHeight, int gridDepth)
{
    m_vDim[0] = gridWidth;
    m_vDim[1] = gridHeight;
    m_vDim[2] = gridDepth;

    m_iMaxDim = _max(_max(m_vDim[0], m_vDim[1]), m_vDim[2]);

    ComputeRowColsForFlat3DTexture(m_vDim[2], &m_iCols, &m_iRows);

    CreateVertexBuffers();
}

void dx103DFluidGrid::CreateVertexBuffers()
{
    // Create layout

    constexpr D3DVERTEXELEMENT9 layoutDesc[]{
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};

    const u32 vSize = FVF::ComputeVertexSize(layoutDesc, 0);

    // UINT numElements = sizeof(layoutDesc)/sizeof(layoutDesc[0]);
    // CreateLayout( layoutDesc, numElements, technique, &layout);

    int index = 0;
    VS_INPUT_FLUIDSIM_STRUCT* renderQuad = nullptr;
    VS_INPUT_FLUIDSIM_STRUCT* slices = nullptr;
    VS_INPUT_FLUIDSIM_STRUCT* boundarySlices = nullptr;
    VS_INPUT_FLUIDSIM_STRUCT* boundaryLines = nullptr;

    m_iNumVerticesRenderQuad = VERTICES_PER_SLICE * m_vDim[2];
    renderQuad = xr_alloc<VS_INPUT_FLUIDSIM_STRUCT>(m_iNumVerticesRenderQuad);

    m_iNumVerticesSlices = VERTICES_PER_SLICE * (m_vDim[2] - 2);
    slices = xr_alloc<VS_INPUT_FLUIDSIM_STRUCT>(m_iNumVerticesSlices);

    m_iNumVerticesBoundarySlices = VERTICES_PER_SLICE * 2;
    boundarySlices = xr_alloc<VS_INPUT_FLUIDSIM_STRUCT>(m_iNumVerticesBoundarySlices);

    m_iNumVerticesBoundaryLines = VERTICES_PER_LINE * LINES_PER_SLICE * (m_vDim[2]);
    boundaryLines = xr_alloc<VS_INPUT_FLUIDSIM_STRUCT>(m_iNumVerticesBoundaryLines);

    VERIFY(renderQuad && m_iNumVerticesSlices && m_iNumVerticesBoundarySlices && m_iNumVerticesBoundaryLines);

    // Vertex buffer for "m_vDim[2]" quads to draw all the slices of the 3D-texture as a flat 3D-texture
    // (used to draw all the individual slices at once to the screen buffer)
    index = 0;
    for (int z = 0; z < m_vDim[2]; z++)
        InitScreenSlice(&renderQuad, z, index);

    // CreateVertexBuffer(sizeof(VS_INPUT_FLUIDSIM_STRUCT)*numVerticesRenderQuad,
    //	D3Dxx_BIND_VERTEX_BUFFER, &renderQuadBuffer, renderQuad, numVerticesRenderQuad));
    CHK_DX(dx10BufferUtils::CreateVertexBuffer(&m_pRenderQuadBuffer, renderQuad, vSize * m_iNumVerticesRenderQuad));
    m_GeomRenderQuad.create(layoutDesc, m_pRenderQuadBuffer, nullptr);

    // Vertex buffer for "m_vDim[2]" quads to draw all the slices to a 3D texture
    // (a Geometry Shader is used to send each quad to the appropriate slice)
    index = 0;
    for (int z = 1; z < m_vDim[2] - 1; z++)
        InitSlice(z, &slices, index);
    VERIFY(index == m_iNumVerticesSlices);
    // V_RETURN(CreateVertexBuffer(sizeof(VS_INPUT_FLUIDSIM_STRUCT)*numVerticesSlices,
    //	D3Dxx_BIND_VERTEX_BUFFER, &slicesBuffer, slices , numVerticesSlices));
    CHK_DX(dx10BufferUtils::CreateVertexBuffer(&m_pSlicesBuffer, slices, vSize * m_iNumVerticesSlices));
    m_GeomSlices.create(layoutDesc, m_pSlicesBuffer, nullptr);

    // Vertex buffers for boundary geometry
    //   2 boundary slices
    index = 0;
    InitBoundaryQuads(&boundarySlices, index);
    VERIFY(index == m_iNumVerticesBoundarySlices);
    // V_RETURN(CreateVertexBuffer(sizeof(VS_INPUT_FLUIDSIM_STRUCT)*numVerticesBoundarySlices,
    //	D3Dxx_BIND_VERTEX_BUFFER, &boundarySlicesBuffer, boundarySlices, numVerticesBoundarySlices));
    CHK_DX(dx10BufferUtils::CreateVertexBuffer(&m_pBoundarySlicesBuffer, boundarySlices, vSize * m_iNumVerticesBoundarySlices));
    m_GeomBoundarySlices.create(layoutDesc, m_pBoundarySlicesBuffer, nullptr);

    //   ( 4 * "m_vDim[2]" ) boundary lines
    index = 0;
    InitBoundaryLines(&boundaryLines, index);
    VERIFY(index == m_iNumVerticesBoundaryLines);
    // V_RETURN(CreateVertexBuffer(sizeof(VS_INPUT_FLUIDSIM_STRUCT)*numVerticesBoundaryLines,
    //	D3Dxx_BIND_VERTEX_BUFFER, &boundaryLinesBuffer, boundaryLines, numVerticesBoundaryLines));
    CHK_DX(dx10BufferUtils::CreateVertexBuffer(&m_pBoundaryLinesBuffer, boundaryLines, vSize * m_iNumVerticesBoundaryLines));
    m_GeomBoundaryLines.create(layoutDesc, m_pBoundaryLinesBuffer, nullptr);

    // cleanup:
    xr_free(renderQuad);

    xr_free(slices);
    slices = nullptr;

    xr_free(boundarySlices);
    boundarySlices = nullptr;

    xr_free(boundaryLines);
    boundaryLines = nullptr;
}

void dx103DFluidGrid::InitScreenSlice(VS_INPUT_FLUIDSIM_STRUCT** vertices, int z, int& index)
{
    VS_INPUT_FLUIDSIM_STRUCT tempVertex1;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex2;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex3;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex4;

    // compute the offset (px, py) in the "flat 3D-texture" space for the slice with given 'z' coordinate
    const int column = z % m_iCols;
    const int row = (int)floorf((float)(z / m_iCols));
    const int px = column * m_vDim[0];
    const int py = row * m_vDim[1];

    const float w = float(m_vDim[0]);
    const float h = float(m_vDim[1]);

    const float Width = float(m_iCols * m_vDim[0]);
    const float Height = float(m_iRows * m_vDim[1]);

    tempVertex1.Pos = D3DXVECTOR3(px * 2.0f / Width - 1.0f, -(py * 2.0f / Height) + 1.0f, 0.0f);
    tempVertex1.Tex = D3DXVECTOR3(0, 0, float(z));

    tempVertex2.Pos = D3DXVECTOR3((px + w) * 2.0f / Width - 1.0f, -((py)*2.0f / Height) + 1.0f, 0.0f);
    tempVertex2.Tex = D3DXVECTOR3(w, 0, float(z));

    tempVertex3.Pos = D3DXVECTOR3((px + w) * 2.0f / Width - 1.0f, -((py + h) * 2.0f / Height) + 1.0f, 0.0f);
    tempVertex3.Tex = D3DXVECTOR3(w, h, float(z));

    tempVertex4.Pos = D3DXVECTOR3((px)*2.0f / Width - 1.0f, -((py + h) * 2.0f / Height) + 1.0f, 0.0f);
    tempVertex4.Tex = D3DXVECTOR3(0, h, float(z));

    (*vertices)[index++] = tempVertex1;
    (*vertices)[index++] = tempVertex2;
    (*vertices)[index++] = tempVertex3;
    (*vertices)[index++] = tempVertex1;
    (*vertices)[index++] = tempVertex3;
    (*vertices)[index++] = tempVertex4;
}

void dx103DFluidGrid::InitSlice(int z, VS_INPUT_FLUIDSIM_STRUCT** vertices, int& index)
{
    VS_INPUT_FLUIDSIM_STRUCT tempVertex1;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex2;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex3;
    VS_INPUT_FLUIDSIM_STRUCT tempVertex4;

    const int w = m_vDim[0];
    const int h = m_vDim[1];

    tempVertex1.Pos = D3DXVECTOR3(1 * 2.0f / w - 1.0f, -1 * 2.0f / h + 1.0f, 0.0f);
    tempVertex1.Tex = D3DXVECTOR3(1.0f, 1.0f, float(z));

    tempVertex2.Pos = D3DXVECTOR3((w - 1.0f) * 2.0f / w - 1.0f, -1 * 2.0f / h + 1.0f, 0.0f);
    tempVertex2.Tex = D3DXVECTOR3((w - 1.0f), 1.0f, float(z));

    tempVertex3.Pos = D3DXVECTOR3((w - 1.0f) * 2.0f / w - 1.0f, -(h - 1) * 2.0f / h + 1.0f, 0.0f);
    tempVertex3.Tex = D3DXVECTOR3((w - 1.0f), (h - 1.0f), float(z));

    tempVertex4.Pos = D3DXVECTOR3(1 * 2.0f / w - 1.0f, -(h - 1.0f) * 2.0f / h + 1.0f, 0.0f);
    tempVertex4.Tex = D3DXVECTOR3(1.0f, (h - 1.0f), float(z));

    (*vertices)[index++] = tempVertex1;
    (*vertices)[index++] = tempVertex2;
    (*vertices)[index++] = tempVertex3;
    (*vertices)[index++] = tempVertex1;
    (*vertices)[index++] = tempVertex3;
    (*vertices)[index++] = tempVertex4;
}

void dx103DFluidGrid::InitLine(float x1, float y1, float x2, float y2, int z, VS_INPUT_FLUIDSIM_STRUCT** vertices, int& index)
{
    VS_INPUT_FLUIDSIM_STRUCT tempVertex;
    const int w = m_vDim[0];
    const int h = m_vDim[1];

    tempVertex.Pos = D3DXVECTOR3(x1 * 2.0f / w - 1.0f, -y1 * 2.0f / h + 1.0f, 0.5f);
    tempVertex.Tex = D3DXVECTOR3(0.0f, 0.0f, float(z));
    (*vertices)[index++] = tempVertex;

    tempVertex.Pos = D3DXVECTOR3(x2 * 2.0f / w - 1.0f, -y2 * 2.0f / h + 1.0f, 0.5f);
    tempVertex.Tex = D3DXVECTOR3(0.0f, 0.0f, float(z));
    (*vertices)[index++] = tempVertex;
}

void dx103DFluidGrid::InitBoundaryQuads(VS_INPUT_FLUIDSIM_STRUCT** vertices, int& index)
{
    InitSlice(0, vertices, index);
    InitSlice(m_vDim[2] - 1, vertices, index);
}

void dx103DFluidGrid::InitBoundaryLines(VS_INPUT_FLUIDSIM_STRUCT** vertices, int& index)
{
    const int w = m_vDim[0];
    const int h = m_vDim[1];

    for (int z = 0; z < m_vDim[2]; z++)
    {
        // bottom
        InitLine(0.0f, 1.0f, float(w), 1.0f, z, vertices, index);
        // top
        InitLine(0.0f, float(h), float(w), float(h), z, vertices, index);
        // left
        InitLine(1.0f, 0.0f, 1.0f, float(h), z, vertices, index);
        // right
        InitLine(float(w), 0.0f, float(w), float(h), z, vertices, index);
    }
}

void dx103DFluidGrid::DrawSlices(CBackend& cmd_list)
{
    // UINT stride[1] = { sizeof(VS_INPUT_FLUIDSIM_STRUCT) };
    // UINT offset[1] = { 0 };
    // DrawPrimitive( D3Dxx_PRIMITIVE_TOPOLOGY_TRIANGLELIST, layout, &slicesBuffer,
    //	stride, offset, 0, numVerticesSlices );

    cmd_list.set_Geometry(m_GeomSlices);
    cmd_list.Render(D3DPT_TRIANGLELIST, 0, m_iNumVerticesSlices / 3);
}

void dx103DFluidGrid::DrawSlicesToScreen(CBackend& cmd_list)
{
    // UINT stride[1] = { sizeof(VS_INPUT_FLUIDSIM_STRUCT) };
    // UINT offset[1] = { 0 };
    // DrawPrimitive( D3Dxx_PRIMITIVE_TOPOLOGY_TRIANGLELIST, layout, &renderQuadBuffer,
    //	stride, offset, 0, numVerticesRenderQuad );

    cmd_list.set_Geometry(m_GeomRenderQuad);
    cmd_list.Render(D3DPT_TRIANGLELIST, 0, m_iNumVerticesRenderQuad / 3);
}

void dx103DFluidGrid::DrawBoundaryQuads(CBackend& cmd_list)
{
    // UINT stride[1] = { sizeof(VS_INPUT_FLUIDSIM_STRUCT) };
    // UINT offset[1] = { 0 };
    // DrawPrimitive( D3Dxx_PRIMITIVE_TOPOLOGY_TRIANGLELIST, layout, &boundarySlicesBuffer,
    //	stride, offset, 0, numVerticesBoundarySlices );

    cmd_list.set_Geometry(m_GeomBoundarySlices);
    cmd_list.Render(D3DPT_TRIANGLELIST, 0, m_iNumVerticesBoundarySlices / 3);
}

void dx103DFluidGrid::DrawBoundaryLines(CBackend& cmd_list)
{
    //	UINT stride[1] = { sizeof(VS_INPUT_FLUIDSIM_STRUCT) };
    //	UINT offset[1] = { 0 };
    //	DrawPrimitive( D3Dxx_PRIMITIVE_TOPOLOGY_LINELIST, layout, &boundaryLinesBuffer,
    //		stride, offset, 0, numVerticesBoundaryLines  );

    cmd_list.set_Geometry(m_GeomBoundaryLines);
    cmd_list.Render(D3DPT_TRIANGLELIST, 0, m_iNumVerticesBoundaryLines / 3);
}

#endif
