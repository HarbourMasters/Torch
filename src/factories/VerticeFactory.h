#pragma once

#include "RawFactory.h"

class VerticeFactory : public RawFactory {
/*
 * Vertex (set up for use with colors)
 */
typedef struct {
	short		ob[3];	/* x, y, z */
	unsigned short	flag;
	short		tc[2];	/* texture coord */
	unsigned char	cn[4];	/* color & alpha */
} Vtx_t;

/*
 * Vertex (set up for use with normals)
 */
typedef struct {
	short		ob[3];	/* x, y, z */
	unsigned short	flag;
	short		tc[2];	/* texture coord */
	signed char	n[3];	/* normal */
	unsigned char   a;      /* alpha  */
} Vtx_tn;

typedef union {
    Vtx_t		v;  /* Use this one for colors  */
    Vtx_tn              n;  /* Use this one for normals */
    long long int	force_structure_alignment;
} Vtx;
public:
    VerticeFactory() = default;
    bool process(LUS::BinaryWriter* write, YAML::Node& data, std::vector<uint8_t>& buffer) override;
};