#pragma once

class CBlender_bloom_build : public IBlender
{
public:
    virtual LPCSTR getComment() { return "INTERNAL: combine to bloom target"; }

    virtual void Compile(CBlender_Compile& C);

    CBlender_bloom_build();
    virtual ~CBlender_bloom_build();
};
