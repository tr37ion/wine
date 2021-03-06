/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d_shader);
WINE_DECLARE_DEBUG_CHANNEL(d3d_bytecode);

#define WINED3D_SM4_INSTRUCTION_MODIFIER        (0x1u << 31)

#define WINED3D_SM4_MODIFIER_AOFFIMMI           0x1
#define WINED3D_SM4_AOFFIMMI_U_SHIFT            9
#define WINED3D_SM4_AOFFIMMI_U_MASK             (0xfu << WINED3D_SM4_AOFFIMMI_U_SHIFT)
#define WINED3D_SM4_AOFFIMMI_V_SHIFT            13
#define WINED3D_SM4_AOFFIMMI_V_MASK             (0xfu << WINED3D_SM4_AOFFIMMI_V_SHIFT)
#define WINED3D_SM4_AOFFIMMI_W_SHIFT            17
#define WINED3D_SM4_AOFFIMMI_W_MASK             (0xfu << WINED3D_SM4_AOFFIMMI_W_SHIFT)

#define WINED3D_SM4_INSTRUCTION_LENGTH_SHIFT    24
#define WINED3D_SM4_INSTRUCTION_LENGTH_MASK     (0x1fu << WINED3D_SM4_INSTRUCTION_LENGTH_SHIFT)

#define WINED3D_SM4_INSTRUCTION_FLAGS_SHIFT     11
#define WINED3D_SM4_INSTRUCTION_FLAGS_MASK      (0x7u << WINED3D_SM4_INSTRUCTION_FLAGS_SHIFT)

#define WINED3D_SM4_RESOURCE_TYPE_SHIFT         11
#define WINED3D_SM4_RESOURCE_TYPE_MASK          (0xfu << WINED3D_SM4_RESOURCE_TYPE_SHIFT)

#define WINED3D_SM4_PRIMITIVE_TYPE_SHIFT        11
#define WINED3D_SM4_PRIMITIVE_TYPE_MASK         (0x7u << WINED3D_SM4_PRIMITIVE_TYPE_SHIFT)

#define WINED3D_SM4_INDEX_TYPE_SHIFT            11
#define WINED3D_SM4_INDEX_TYPE_MASK             (0x1u << WINED3D_SM4_INDEX_TYPE_SHIFT)

#define WINED3D_SM4_SAMPLER_MODE_SHIFT          11
#define WINED3D_SM4_SAMPLER_MODE_MASK           (0xfu << WINED3D_SM4_SAMPLER_MODE_SHIFT)

#define WINED3D_SM4_SHADER_DATA_TYPE_SHIFT      11
#define WINED3D_SM4_SHADER_DATA_TYPE_MASK       (0xfu << WINED3D_SM4_SHADER_DATA_TYPE_SHIFT)

#define WINED3D_SM4_INTERPOLATION_MODE_SHIFT    11
#define WINED3D_SM4_INTERPOLATION_MODE_MASK     (0xfu << WINED3D_SM4_INTERPOLATION_MODE_SHIFT)

#define WINED3D_SM4_GLOBAL_FLAGS_SHIFT          11
#define WINED3D_SM4_GLOBAL_FLAGS_MASK           (0xffu << WINED3D_SM4_GLOBAL_FLAGS_SHIFT)

#define WINED3D_SM5_CONTROL_POINT_COUNT_SHIFT   11
#define WINED3D_SM5_CONTROL_POINT_COUNT_MASK    (0xffu << WINED3D_SM5_CONTROL_POINT_COUNT_SHIFT)

#define WINED3D_SM5_TESSELLATOR_SHIFT           11
#define WINED3D_SM5_TESSELLATOR_MASK            (0xfu << WINED3D_SM5_TESSELLATOR_SHIFT)

#define WINED3D_SM4_OPCODE_MASK                 0xff

#define WINED3D_SM4_REGISTER_MODIFIER           (0x1u << 31)

#define WINED3D_SM4_ADDRESSING_SHIFT1           25
#define WINED3D_SM4_ADDRESSING_MASK1            (0x3u << WINED3D_SM4_ADDRESSING_SHIFT1)

#define WINED3D_SM4_ADDRESSING_SHIFT0           22
#define WINED3D_SM4_ADDRESSING_MASK0            (0x3u << WINED3D_SM4_ADDRESSING_SHIFT0)

#define WINED3D_SM4_REGISTER_ORDER_SHIFT        20
#define WINED3D_SM4_REGISTER_ORDER_MASK         (0x3u << WINED3D_SM4_REGISTER_ORDER_SHIFT)

#define WINED3D_SM4_REGISTER_TYPE_SHIFT         12
#define WINED3D_SM4_REGISTER_TYPE_MASK          (0xffu << WINED3D_SM4_REGISTER_TYPE_SHIFT)

#define WINED3D_SM4_SWIZZLE_TYPE_SHIFT          2
#define WINED3D_SM4_SWIZZLE_TYPE_MASK           (0x3u << WINED3D_SM4_SWIZZLE_TYPE_SHIFT)

#define WINED3D_SM4_IMMCONST_TYPE_SHIFT         0
#define WINED3D_SM4_IMMCONST_TYPE_MASK          (0x3u << WINED3D_SM4_IMMCONST_TYPE_SHIFT)

#define WINED3D_SM4_WRITEMASK_SHIFT             4
#define WINED3D_SM4_WRITEMASK_MASK              (0xfu << WINED3D_SM4_WRITEMASK_SHIFT)

#define WINED3D_SM4_SWIZZLE_SHIFT               4
#define WINED3D_SM4_SWIZZLE_MASK                (0xffu << WINED3D_SM4_SWIZZLE_SHIFT)

#define WINED3D_SM4_VERSION_MAJOR(version)      (((version) >> 4) & 0xf)
#define WINED3D_SM4_VERSION_MINOR(version)      (((version) >> 0) & 0xf)

#define WINED3D_SM4_ADDRESSING_RELATIVE         0x2
#define WINED3D_SM4_ADDRESSING_OFFSET           0x1

#define WINED3D_SM4_INSTRUCTION_FLAG_SATURATE   0x4

#define WINED3D_SM4_CONDITIONAL_NZ              (0x1u << 18)

enum wined3d_sm4_opcode
{
    WINED3D_SM4_OP_ADD                              = 0x00,
    WINED3D_SM4_OP_AND                              = 0x01,
    WINED3D_SM4_OP_BREAK                            = 0x02,
    WINED3D_SM4_OP_BREAKC                           = 0x03,
    WINED3D_SM4_OP_CASE                             = 0x06,
    WINED3D_SM4_OP_CUT                              = 0x09,
    WINED3D_SM4_OP_DEFAULT                          = 0x0a,
    WINED3D_SM4_OP_DERIV_RTX                        = 0x0b,
    WINED3D_SM4_OP_DERIV_RTY                        = 0x0c,
    WINED3D_SM4_OP_DISCARD                          = 0x0d,
    WINED3D_SM4_OP_DIV                              = 0x0e,
    WINED3D_SM4_OP_DP2                              = 0x0f,
    WINED3D_SM4_OP_DP3                              = 0x10,
    WINED3D_SM4_OP_DP4                              = 0x11,
    WINED3D_SM4_OP_ELSE                             = 0x12,
    WINED3D_SM4_OP_EMIT                             = 0x13,
    WINED3D_SM4_OP_ENDIF                            = 0x15,
    WINED3D_SM4_OP_ENDLOOP                          = 0x16,
    WINED3D_SM4_OP_ENDSWITCH                        = 0x17,
    WINED3D_SM4_OP_EQ                               = 0x18,
    WINED3D_SM4_OP_EXP                              = 0x19,
    WINED3D_SM4_OP_FRC                              = 0x1a,
    WINED3D_SM4_OP_FTOI                             = 0x1b,
    WINED3D_SM4_OP_FTOU                             = 0x1c,
    WINED3D_SM4_OP_GE                               = 0x1d,
    WINED3D_SM4_OP_IADD                             = 0x1e,
    WINED3D_SM4_OP_IF                               = 0x1f,
    WINED3D_SM4_OP_IEQ                              = 0x20,
    WINED3D_SM4_OP_IGE                              = 0x21,
    WINED3D_SM4_OP_ILT                              = 0x22,
    WINED3D_SM4_OP_IMAD                             = 0x23,
    WINED3D_SM4_OP_IMAX                             = 0x24,
    WINED3D_SM4_OP_IMIN                             = 0x25,
    WINED3D_SM4_OP_IMUL                             = 0x26,
    WINED3D_SM4_OP_INE                              = 0x27,
    WINED3D_SM4_OP_INEG                             = 0x28,
    WINED3D_SM4_OP_ISHL                             = 0x29,
    WINED3D_SM4_OP_ISHR                             = 0x2a,
    WINED3D_SM4_OP_ITOF                             = 0x2b,
    WINED3D_SM4_OP_LD                               = 0x2d,
    WINED3D_SM4_OP_LD2DMS                           = 0x2e,
    WINED3D_SM4_OP_LOG                              = 0x2f,
    WINED3D_SM4_OP_LOOP                             = 0x30,
    WINED3D_SM4_OP_LT                               = 0x31,
    WINED3D_SM4_OP_MAD                              = 0x32,
    WINED3D_SM4_OP_MIN                              = 0x33,
    WINED3D_SM4_OP_MAX                              = 0x34,
    WINED3D_SM4_OP_SHADER_DATA                      = 0x35,
    WINED3D_SM4_OP_MOV                              = 0x36,
    WINED3D_SM4_OP_MOVC                             = 0x37,
    WINED3D_SM4_OP_MUL                              = 0x38,
    WINED3D_SM4_OP_NE                               = 0x39,
    WINED3D_SM4_OP_NOT                              = 0x3b,
    WINED3D_SM4_OP_OR                               = 0x3c,
    WINED3D_SM4_OP_RESINFO                          = 0x3d,
    WINED3D_SM4_OP_RET                              = 0x3e,
    WINED3D_SM4_OP_ROUND_NE                         = 0x40,
    WINED3D_SM4_OP_ROUND_NI                         = 0x41,
    WINED3D_SM4_OP_ROUND_PI                         = 0x42,
    WINED3D_SM4_OP_ROUND_Z                          = 0x43,
    WINED3D_SM4_OP_RSQ                              = 0x44,
    WINED3D_SM4_OP_SAMPLE                           = 0x45,
    WINED3D_SM4_OP_SAMPLE_C                         = 0x46,
    WINED3D_SM4_OP_SAMPLE_C_LZ                      = 0x47,
    WINED3D_SM4_OP_SAMPLE_LOD                       = 0x48,
    WINED3D_SM4_OP_SAMPLE_GRAD                      = 0x49,
    WINED3D_SM4_OP_SAMPLE_B                         = 0x4a,
    WINED3D_SM4_OP_SQRT                             = 0x4b,
    WINED3D_SM4_OP_SWITCH                           = 0x4c,
    WINED3D_SM4_OP_SINCOS                           = 0x4d,
    WINED3D_SM4_OP_UDIV                             = 0x4e,
    WINED3D_SM4_OP_ULT                              = 0x4f,
    WINED3D_SM4_OP_UGE                              = 0x50,
    WINED3D_SM4_OP_UMAX                             = 0x53,
    WINED3D_SM4_OP_USHR                             = 0x55,
    WINED3D_SM4_OP_UTOF                             = 0x56,
    WINED3D_SM4_OP_XOR                              = 0x57,
    WINED3D_SM4_OP_DCL_RESOURCE                     = 0x58,
    WINED3D_SM4_OP_DCL_CONSTANT_BUFFER              = 0x59,
    WINED3D_SM4_OP_DCL_SAMPLER                      = 0x5a,
    WINED3D_SM4_OP_DCL_OUTPUT_TOPOLOGY              = 0x5c,
    WINED3D_SM4_OP_DCL_INPUT_PRIMITIVE              = 0x5d,
    WINED3D_SM4_OP_DCL_VERTICES_OUT                 = 0x5e,
    WINED3D_SM4_OP_DCL_INPUT                        = 0x5f,
    WINED3D_SM4_OP_DCL_INPUT_SGV                    = 0x60,
    WINED3D_SM4_OP_DCL_INPUT_SIV                    = 0x61,
    WINED3D_SM4_OP_DCL_INPUT_PS                     = 0x62,
    WINED3D_SM4_OP_DCL_INPUT_PS_SGV                 = 0x63,
    WINED3D_SM4_OP_DCL_INPUT_PS_SIV                 = 0x64,
    WINED3D_SM4_OP_DCL_OUTPUT                       = 0x65,
    WINED3D_SM4_OP_DCL_OUTPUT_SIV                   = 0x67,
    WINED3D_SM4_OP_DCL_TEMPS                        = 0x68,
    WINED3D_SM4_OP_DCL_GLOBAL_FLAGS                 = 0x6a,
    WINED3D_SM4_OP_GATHER4                          = 0x6d,
    WINED3D_SM5_OP_HS_DECLS                         = 0x71,
    WINED3D_SM5_OP_HS_CONTROL_POINT_PHASE           = 0x72,
    WINED3D_SM5_OP_HS_FORK_PHASE                    = 0x73,
    WINED3D_SM5_OP_HS_JOIN_PHASE                    = 0x74,
    WINED3D_SM5_OP_BUFINFO                          = 0x79,
    WINED3D_SM5_OP_DERIV_RTX_COARSE                 = 0x7a,
    WINED3D_SM5_OP_DERIV_RTX_FINE                   = 0x7b,
    WINED3D_SM5_OP_DERIV_RTY_COARSE                 = 0x7c,
    WINED3D_SM5_OP_DERIV_RTY_FINE                   = 0x7d,
    WINED3D_SM5_OP_GATHER4_C                        = 0x7e,
    WINED3D_SM5_OP_BFI                              = 0x8c,
    WINED3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT    = 0x93,
    WINED3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT   = 0x94,
    WINED3D_SM5_OP_DCL_TESSELLATOR_DOMAIN           = 0x95,
    WINED3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING     = 0x96,
    WINED3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE = 0x97,
    WINED3D_SM5_OP_DCL_HS_MAX_TESSFACTOR            = 0x98,
    WINED3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT = 0x99,
    WINED3D_SM5_OP_DCL_UAV_TYPED                    = 0x9c,
    WINED3D_SM5_OP_DCL_RESOURCE_STRUCTURED          = 0xa2,
    WINED3D_SM5_OP_STORE_UAV_TYPED                  = 0xa4,
    WINED3D_SM5_OP_LD_RAW                           = 0xa5,
    WINED3D_SM5_OP_STORE_RAW                        = 0xa6,
    WINED3D_SM5_OP_LD_STRUCTURED                    = 0xa7,
    WINED3D_SM5_OP_STORE_STRUCTURED                 = 0xa8,
    WINED3D_SM5_OP_IMM_ATOMIC_CONSUME               = 0xb3,
};

enum wined3d_sm4_register_type
{
    WINED3D_SM4_RT_TEMP                    = 0x0,
    WINED3D_SM4_RT_INPUT                   = 0x1,
    WINED3D_SM4_RT_OUTPUT                  = 0x2,
    WINED3D_SM4_RT_IMMCONST                = 0x4,
    WINED3D_SM4_RT_SAMPLER                 = 0x6,
    WINED3D_SM4_RT_RESOURCE                = 0x7,
    WINED3D_SM4_RT_CONSTBUFFER             = 0x8,
    WINED3D_SM4_RT_IMMCONSTBUFFER          = 0x9,
    WINED3D_SM4_RT_PRIMID                  = 0xb,
    WINED3D_SM4_RT_DEPTHOUT                = 0xc,
    WINED3D_SM4_RT_NULL                    = 0xd,
    WINED3D_SM5_RT_OUTPUT_CONTROL_POINT_ID = 0x16,
    WINED3D_SM5_RT_FORK_INSTANCE_ID        = 0x17,
    WINED3D_SM5_RT_INPUT_CONTROL_POINT     = 0x19,
    WINED3D_SM5_RT_PATCH_CONSTANT_DATA     = 0x1b,
    WINED3D_SM5_RT_DOMAIN_LOCATION         = 0x1c,
    WINED3D_SM5_RT_UAV                     = 0x1e,
    WINED3D_SM5_RT_SHARED_MEMORY           = 0x1f,
};

enum wined3d_sm4_output_primitive_type
{
    WINED3D_SM4_OUTPUT_PT_POINTLIST     = 0x1,
    WINED3D_SM4_OUTPUT_PT_LINELIST      = 0x3,
    WINED3D_SM4_OUTPUT_PT_TRIANGLESTRIP = 0x5,
};

enum wined3d_sm4_input_primitive_type
{
    WINED3D_SM4_INPUT_PT_POINT          = 0x1,
    WINED3D_SM4_INPUT_PT_LINE           = 0x2,
    WINED3D_SM4_INPUT_PT_TRIANGLE       = 0x3,
    WINED3D_SM4_INPUT_PT_LINEADJ        = 0x6,
    WINED3D_SM4_INPUT_PT_TRIANGLEADJ    = 0x7,
};

enum wined3d_sm4_swizzle_type
{
    WINED3D_SM4_SWIZZLE_NONE            = 0x0,
    WINED3D_SM4_SWIZZLE_VEC4            = 0x1,
    WINED3D_SM4_SWIZZLE_SCALAR          = 0x2,
};

enum wined3d_sm4_immconst_type
{
    WINED3D_SM4_IMMCONST_SCALAR = 0x1,
    WINED3D_SM4_IMMCONST_VEC4   = 0x2,
};

enum wined3d_sm4_resource_type
{
    WINED3D_SM4_RESOURCE_BUFFER             = 0x1,
    WINED3D_SM4_RESOURCE_TEXTURE_1D         = 0x2,
    WINED3D_SM4_RESOURCE_TEXTURE_2D         = 0x3,
    WINED3D_SM4_RESOURCE_TEXTURE_2DMS       = 0x4,
    WINED3D_SM4_RESOURCE_TEXTURE_3D         = 0x5,
    WINED3D_SM4_RESOURCE_TEXTURE_CUBE       = 0x6,
    WINED3D_SM4_RESOURCE_TEXTURE_1DARRAY    = 0x7,
    WINED3D_SM4_RESOURCE_TEXTURE_2DARRAY    = 0x8,
    WINED3D_SM4_RESOURCE_TEXTURE_2DMSARRAY  = 0x9,
};

enum wined3d_sm4_data_type
{
    WINED3D_SM4_DATA_UNORM  = 0x1,
    WINED3D_SM4_DATA_SNORM  = 0x2,
    WINED3D_SM4_DATA_INT    = 0x3,
    WINED3D_SM4_DATA_UINT   = 0x4,
    WINED3D_SM4_DATA_FLOAT  = 0x5,
};

enum wined3d_sm4_sampler_mode
{
    WINED3D_SM4_SAMPLER_DEFAULT    = 0x0,
    WINED3D_SM4_SAMPLER_COMPARISON = 0x1,
};

enum wined3d_sm4_shader_data_type
{
    WINED3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER = 0x3,
    WINED3D_SM4_SHADER_DATA_MESSAGE                   = 0x4,
};

struct wined3d_shader_src_param_entry
{
    struct list entry;
    struct wined3d_shader_src_param param;
};

struct wined3d_sm4_data
{
    struct wined3d_shader_version shader_version;
    const DWORD *end;

    unsigned int output_map[MAX_REG_OUTPUT];

    struct wined3d_shader_src_param src_param[5];
    struct wined3d_shader_dst_param dst_param[2];
    struct list src_free;
    struct list src;
    struct wined3d_shader_immediate_constant_buffer icb;
};

struct wined3d_sm4_opcode_info
{
    enum wined3d_sm4_opcode opcode;
    enum WINED3D_SHADER_INSTRUCTION_HANDLER handler_idx;
    const char *dst_info;
    const char *src_info;
    void (*read_opcode_func)(struct wined3d_shader_instruction *ins,
            DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
            struct wined3d_sm4_data *priv);
};

static const enum wined3d_primitive_type output_primitive_type_table[] =
{
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* WINED3D_SM4_OUTPUT_PT_POINTLIST */       WINED3D_PT_POINTLIST,
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* WINED3D_SM4_OUTPUT_PT_LINELIST */        WINED3D_PT_LINELIST,
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* WINED3D_SM4_OUTPUT_PT_TRIANGLESTRIP */   WINED3D_PT_TRIANGLESTRIP,
};

static const enum wined3d_primitive_type input_primitive_type_table[] =
{
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* WINED3D_SM4_INPUT_PT_POINT */            WINED3D_PT_POINTLIST,
    /* WINED3D_SM4_INPUT_PT_LINE */             WINED3D_PT_LINELIST,
    /* WINED3D_SM4_INPUT_PT_TRIANGLE */         WINED3D_PT_TRIANGLELIST,
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* UNKNOWN */                               WINED3D_PT_UNDEFINED,
    /* WINED3D_SM4_INPUT_PT_LINEADJ */          WINED3D_PT_LINELIST_ADJ,
    /* WINED3D_SM4_INPUT_PT_TRIANGLEADJ */      WINED3D_PT_TRIANGLELIST_ADJ,
};

static const enum wined3d_shader_resource_type resource_type_table[] =
{
    /* 0 */                                         WINED3D_SHADER_RESOURCE_NONE,
    /* WINED3D_SM4_RESOURCE_BUFFER */               WINED3D_SHADER_RESOURCE_BUFFER,
    /* WINED3D_SM4_RESOURCE_TEXTURE_1D */           WINED3D_SHADER_RESOURCE_TEXTURE_1D,
    /* WINED3D_SM4_RESOURCE_TEXTURE_2D */           WINED3D_SHADER_RESOURCE_TEXTURE_2D,
    /* WINED3D_SM4_RESOURCE_TEXTURE_2DMS */         WINED3D_SHADER_RESOURCE_TEXTURE_2DMS,
    /* WINED3D_SM4_RESOURCE_TEXTURE_3D */           WINED3D_SHADER_RESOURCE_TEXTURE_3D,
    /* WINED3D_SM4_RESOURCE_TEXTURE_CUBE */         WINED3D_SHADER_RESOURCE_TEXTURE_CUBE,
    /* WINED3D_SM4_RESOURCE_TEXTURE_1DARRAY */      WINED3D_SHADER_RESOURCE_TEXTURE_1DARRAY,
    /* WINED3D_SM4_RESOURCE_TEXTURE_2DARRAY */      WINED3D_SHADER_RESOURCE_TEXTURE_2DARRAY,
    /* WINED3D_SM4_RESOURCE_TEXTURE_2DMSARRAY */    WINED3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY,
};

static const enum wined3d_data_type data_type_table[] =
{
    /* 0 */                         WINED3D_DATA_FLOAT,
    /* WINED3D_SM4_DATA_UNORM */    WINED3D_DATA_UNORM,
    /* WINED3D_SM4_DATA_SNORM */    WINED3D_DATA_SNORM,
    /* WINED3D_SM4_DATA_INT */      WINED3D_DATA_INT,
    /* WINED3D_SM4_DATA_UINT */     WINED3D_DATA_UINT,
    /* WINED3D_SM4_DATA_FLOAT */    WINED3D_DATA_FLOAT,
};

static BOOL shader_sm4_read_src_param(struct wined3d_sm4_data *priv, const DWORD **ptr,
        enum wined3d_data_type data_type, struct wined3d_shader_src_param *src_param);
static BOOL shader_sm4_read_dst_param(struct wined3d_sm4_data *priv, const DWORD **ptr,
        enum wined3d_data_type data_type, struct wined3d_shader_dst_param *dst_param);

static void shader_sm4_read_conditional_op(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    shader_sm4_read_src_param(priv, &tokens, WINED3D_DATA_UINT, &priv->src_param[0]);
    ins->flags = (opcode_token & WINED3D_SM4_CONDITIONAL_NZ) ?
            WINED3D_SHADER_CONDITIONAL_OP_NZ : WINED3D_SHADER_CONDITIONAL_OP_Z;
}

static void shader_sm4_read_shader_data(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    enum wined3d_sm4_shader_data_type type;
    unsigned int icb_size;

    type = (opcode_token & WINED3D_SM4_SHADER_DATA_TYPE_MASK) >> WINED3D_SM4_SHADER_DATA_TYPE_SHIFT;
    if (type != WINED3D_SM4_SHADER_DATA_IMMEDIATE_CONSTANT_BUFFER)
    {
        FIXME("Unhandled shader data type %#x.\n", type);
        ins->handler_idx = WINED3DSIH_TABLE_SIZE;
        return;
    }

    ++tokens;
    icb_size = token_count - 1;
    if (icb_size % 4 || icb_size > MAX_IMMEDIATE_CONSTANT_BUFFER_SIZE)
    {
        FIXME("Unexpected immediate constant buffer size %u.\n", icb_size);
        ins->handler_idx = WINED3DSIH_TABLE_SIZE;
        return;
    }

    priv->icb.element_count = icb_size;
    memcpy(priv->icb.data, tokens, sizeof(*tokens) * icb_size);
    ins->declaration.icb = &priv->icb;
}

static void shader_sm4_read_dcl_resource(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    enum wined3d_sm4_resource_type resource_type;
    enum wined3d_sm4_data_type data_type;
    enum wined3d_data_type reg_data_type;
    DWORD components;

    resource_type = (opcode_token & WINED3D_SM4_RESOURCE_TYPE_MASK) >> WINED3D_SM4_RESOURCE_TYPE_SHIFT;
    if (!resource_type || (resource_type >= ARRAY_SIZE(resource_type_table)))
    {
        FIXME("Unhandled resource type %#x.\n", resource_type);
        ins->declaration.semantic.resource_type = WINED3D_SHADER_RESOURCE_NONE;
    }
    else
    {
        ins->declaration.semantic.resource_type = resource_type_table[resource_type];
    }
    reg_data_type = opcode == WINED3D_SM4_OP_DCL_RESOURCE ? WINED3D_DATA_RESOURCE : WINED3D_DATA_UAV;
    shader_sm4_read_dst_param(priv, &tokens, reg_data_type, &ins->declaration.semantic.reg);

    components = *tokens++;
    if ((components & 0xfff0) != (components & 0xf) * 0x1110)
        FIXME("Components (%#x) have different data types.\n", components);
    data_type = components & 0xf;

    if (!data_type || (data_type >= ARRAY_SIZE(data_type_table)))
    {
        FIXME("Unhandled data type %#x.\n", data_type);
        ins->declaration.semantic.resource_data_type = WINED3D_DATA_FLOAT;
    }
    else
    {
        ins->declaration.semantic.resource_data_type = data_type_table[data_type];
    }
}

static void shader_sm4_read_dcl_constant_buffer(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    shader_sm4_read_src_param(priv, &tokens, WINED3D_DATA_FLOAT, &ins->declaration.src);
    if (opcode_token & WINED3D_SM4_INDEX_TYPE_MASK)
        ins->flags |= WINED3DSI_INDEXED_DYNAMIC;
}

static void shader_sm4_read_dcl_sampler(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->flags = (opcode_token & WINED3D_SM4_SAMPLER_MODE_MASK) >> WINED3D_SM4_SAMPLER_MODE_SHIFT;
    if (ins->flags & ~WINED3D_SM4_SAMPLER_COMPARISON)
        FIXME("Unhandled sampler mode %#x.\n", ins->flags);
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_SAMPLER, &ins->declaration.dst);
}

static void shader_sm4_read_dcl_output_topology(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    enum wined3d_sm4_output_primitive_type primitive_type;

    primitive_type = (opcode_token & WINED3D_SM4_PRIMITIVE_TYPE_MASK) >> WINED3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (primitive_type >= sizeof(output_primitive_type_table) / sizeof(*output_primitive_type_table))
    {
        FIXME("Unhandled output primitive type %#x.\n", primitive_type);
        ins->declaration.primitive_type = WINED3D_PT_UNDEFINED;
    }
    else
    {
        ins->declaration.primitive_type = output_primitive_type_table[primitive_type];
    }
}

static void shader_sm4_read_dcl_input_primitive(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    enum wined3d_sm4_input_primitive_type primitive_type;

    primitive_type = (opcode_token & WINED3D_SM4_PRIMITIVE_TYPE_MASK) >> WINED3D_SM4_PRIMITIVE_TYPE_SHIFT;
    if (primitive_type >= sizeof(input_primitive_type_table) / sizeof(*input_primitive_type_table))
    {
        FIXME("Unhandled input primitive type %#x.\n", primitive_type);
        ins->declaration.primitive_type = WINED3D_PT_UNDEFINED;
    }
    else
    {
        ins->declaration.primitive_type = input_primitive_type_table[primitive_type];
    }
}

static void shader_sm4_read_declaration_count(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.count = *tokens;
}

static void shader_sm4_read_declaration_dst(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_FLOAT, &ins->declaration.dst);
}

static void shader_sm4_read_declaration_register_semantic(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_FLOAT, &ins->declaration.register_semantic.reg);
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_input_ps(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->flags = (opcode_token & WINED3D_SM4_INTERPOLATION_MODE_MASK) >> WINED3D_SM4_INTERPOLATION_MODE_SHIFT;
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_FLOAT, &ins->declaration.dst);
}

static void shader_sm4_read_dcl_input_ps_siv(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->flags = (opcode_token & WINED3D_SM4_INTERPOLATION_MODE_MASK) >> WINED3D_SM4_INTERPOLATION_MODE_SHIFT;
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_FLOAT, &ins->declaration.register_semantic.reg);
    ins->declaration.register_semantic.sysval_semantic = *tokens;
}

static void shader_sm4_read_dcl_global_flags(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->flags = (opcode_token & WINED3D_SM4_GLOBAL_FLAGS_MASK) >> WINED3D_SM4_GLOBAL_FLAGS_SHIFT;
}

static void shader_sm5_read_control_point_count(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.count = (opcode_token & WINED3D_SM5_CONTROL_POINT_COUNT_MASK)
            >> WINED3D_SM5_CONTROL_POINT_COUNT_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_domain(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.tessellator_domain = (opcode_token & WINED3D_SM5_TESSELLATOR_MASK)
        >> WINED3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_partitioning(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.tessellator_partitioning = (opcode_token & WINED3D_SM5_TESSELLATOR_MASK)
            >> WINED3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_tessellator_output_primitive(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.tessellator_output_primitive = (opcode_token & WINED3D_SM5_TESSELLATOR_MASK)
            >> WINED3D_SM5_TESSELLATOR_SHIFT;
}

static void shader_sm5_read_dcl_hs_max_tessfactor(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    ins->declaration.max_tessellation_factor = *(float *)tokens;
}

static void shader_sm5_read_dcl_resource_structured(struct wined3d_shader_instruction *ins,
        DWORD opcode, DWORD opcode_token, const DWORD *tokens, unsigned int token_count,
        struct wined3d_sm4_data *priv)
{
    shader_sm4_read_dst_param(priv, &tokens, WINED3D_DATA_RESOURCE, &ins->declaration.structured_resource.reg);
    ins->declaration.structured_resource.byte_stride = *tokens;
}

/*
 * f -> WINED3D_DATA_FLOAT
 * i -> WINED3D_DATA_INT
 * u -> WINED3D_DATA_UINT
 * R -> WINED3D_DATA_RESOURCE
 * S -> WINED3D_DATA_SAMPLER
 * U -> WINED3D_DATA_UAV
 */
static const struct wined3d_sm4_opcode_info opcode_table[] =
{
    {WINED3D_SM4_OP_ADD,                              WINED3DSIH_ADD,                              "f",    "ff"},
    {WINED3D_SM4_OP_AND,                              WINED3DSIH_AND,                              "u",    "uu"},
    {WINED3D_SM4_OP_BREAK,                            WINED3DSIH_BREAK,                            "",     ""},
    {WINED3D_SM4_OP_BREAKC,                           WINED3DSIH_BREAKP,                           "",     "u",
            shader_sm4_read_conditional_op},
    {WINED3D_SM4_OP_CASE,                             WINED3DSIH_CASE,                             "",     "u"},
    {WINED3D_SM4_OP_CUT,                              WINED3DSIH_CUT,                              "",     ""},
    {WINED3D_SM4_OP_DEFAULT,                          WINED3DSIH_DEFAULT,                          "",     ""},
    {WINED3D_SM4_OP_DERIV_RTX,                        WINED3DSIH_DSX,                              "f",    "f"},
    {WINED3D_SM4_OP_DERIV_RTY,                        WINED3DSIH_DSY,                              "f",    "f"},
    {WINED3D_SM4_OP_DISCARD,                          WINED3DSIH_TEXKILL,                          "",     "u"},
    {WINED3D_SM4_OP_DIV,                              WINED3DSIH_DIV,                              "f",    "ff"},
    {WINED3D_SM4_OP_DP2,                              WINED3DSIH_DP2,                              "f",    "ff"},
    {WINED3D_SM4_OP_DP3,                              WINED3DSIH_DP3,                              "f",    "ff"},
    {WINED3D_SM4_OP_DP4,                              WINED3DSIH_DP4,                              "f",    "ff"},
    {WINED3D_SM4_OP_ELSE,                             WINED3DSIH_ELSE,                             "",     ""},
    {WINED3D_SM4_OP_EMIT,                             WINED3DSIH_EMIT,                             "",     ""},
    {WINED3D_SM4_OP_ENDIF,                            WINED3DSIH_ENDIF,                            "",     ""},
    {WINED3D_SM4_OP_ENDLOOP,                          WINED3DSIH_ENDLOOP,                          "",     ""},
    {WINED3D_SM4_OP_ENDSWITCH,                        WINED3DSIH_ENDSWITCH,                        "",     ""},
    {WINED3D_SM4_OP_EQ,                               WINED3DSIH_EQ,                               "u",    "ff"},
    {WINED3D_SM4_OP_EXP,                              WINED3DSIH_EXP,                              "f",    "f"},
    {WINED3D_SM4_OP_FRC,                              WINED3DSIH_FRC,                              "f",    "f"},
    {WINED3D_SM4_OP_FTOI,                             WINED3DSIH_FTOI,                             "i",    "f"},
    {WINED3D_SM4_OP_FTOU,                             WINED3DSIH_FTOU,                             "u",    "f"},
    {WINED3D_SM4_OP_GE,                               WINED3DSIH_GE,                               "u",    "ff"},
    {WINED3D_SM4_OP_IADD,                             WINED3DSIH_IADD,                             "i",    "ii"},
    {WINED3D_SM4_OP_IF,                               WINED3DSIH_IF,                               "",     "u",
            shader_sm4_read_conditional_op},
    {WINED3D_SM4_OP_IEQ,                              WINED3DSIH_IEQ,                              "u",    "ii"},
    {WINED3D_SM4_OP_IGE,                              WINED3DSIH_IGE,                              "u",    "ii"},
    {WINED3D_SM4_OP_ILT,                              WINED3DSIH_ILT,                              "u",    "ii"},
    {WINED3D_SM4_OP_IMAD,                             WINED3DSIH_IMAD,                             "i",    "iii"},
    {WINED3D_SM4_OP_IMAX,                             WINED3DSIH_IMAX,                             "i",    "ii"},
    {WINED3D_SM4_OP_IMIN,                             WINED3DSIH_IMIN,                             "i",    "ii"},
    {WINED3D_SM4_OP_IMUL,                             WINED3DSIH_IMUL,                             "ii",   "ii"},
    {WINED3D_SM4_OP_INE,                              WINED3DSIH_INE,                              "u",    "ii"},
    {WINED3D_SM4_OP_INEG,                             WINED3DSIH_INEG,                             "i",    "i"},
    {WINED3D_SM4_OP_ISHL,                             WINED3DSIH_ISHL,                             "i",    "ii"},
    {WINED3D_SM4_OP_ISHR,                             WINED3DSIH_ISHR,                             "i",    "ii"},
    {WINED3D_SM4_OP_ITOF,                             WINED3DSIH_ITOF,                             "f",    "i"},
    {WINED3D_SM4_OP_LD,                               WINED3DSIH_LD,                               "u",    "iR"},
    {WINED3D_SM4_OP_LD2DMS,                           WINED3DSIH_LD2DMS,                           "u",    "iRi"},
    {WINED3D_SM4_OP_LOG,                              WINED3DSIH_LOG,                              "f",    "f"},
    {WINED3D_SM4_OP_LOOP,                             WINED3DSIH_LOOP,                             "",     ""},
    {WINED3D_SM4_OP_LT,                               WINED3DSIH_LT,                               "u",    "ff"},
    {WINED3D_SM4_OP_MAD,                              WINED3DSIH_MAD,                              "f",    "fff"},
    {WINED3D_SM4_OP_MIN,                              WINED3DSIH_MIN,                              "f",    "ff"},
    {WINED3D_SM4_OP_MAX,                              WINED3DSIH_MAX,                              "f",    "ff"},
    {WINED3D_SM4_OP_SHADER_DATA,                      WINED3DSIH_DCL_IMMEDIATE_CONSTANT_BUFFER,    "",     "",
            shader_sm4_read_shader_data},
    {WINED3D_SM4_OP_MOV,                              WINED3DSIH_MOV,                              "f",    "f"},
    {WINED3D_SM4_OP_MOVC,                             WINED3DSIH_MOVC,                             "f",    "uff"},
    {WINED3D_SM4_OP_MUL,                              WINED3DSIH_MUL,                              "f",    "ff"},
    {WINED3D_SM4_OP_NE,                               WINED3DSIH_NE,                               "u",    "ff"},
    {WINED3D_SM4_OP_NOT,                              WINED3DSIH_NOT,                              "u",    "u"},
    {WINED3D_SM4_OP_OR,                               WINED3DSIH_OR,                               "u",    "uu"},
    {WINED3D_SM4_OP_RESINFO,                          WINED3DSIH_RESINFO,                          "f",    "iR"},
    {WINED3D_SM4_OP_RET,                              WINED3DSIH_RET,                              "",     ""},
    {WINED3D_SM4_OP_ROUND_NE,                         WINED3DSIH_ROUND_NE,                         "f",    "f"},
    {WINED3D_SM4_OP_ROUND_NI,                         WINED3DSIH_ROUND_NI,                         "f",    "f"},
    {WINED3D_SM4_OP_ROUND_PI,                         WINED3DSIH_ROUND_PI,                         "f",    "f"},
    {WINED3D_SM4_OP_ROUND_Z,                          WINED3DSIH_ROUND_Z,                          "f",    "f"},
    {WINED3D_SM4_OP_RSQ,                              WINED3DSIH_RSQ,                              "f",    "f"},
    {WINED3D_SM4_OP_SAMPLE,                           WINED3DSIH_SAMPLE,                           "u",    "fRS"},
    {WINED3D_SM4_OP_SAMPLE_C,                         WINED3DSIH_SAMPLE_C,                         "f",    "fRSf"},
    {WINED3D_SM4_OP_SAMPLE_C_LZ,                      WINED3DSIH_SAMPLE_C_LZ,                      "f",    "fRSf"},
    {WINED3D_SM4_OP_SAMPLE_LOD,                       WINED3DSIH_SAMPLE_LOD,                       "u",    "fRSf"},
    {WINED3D_SM4_OP_SAMPLE_GRAD,                      WINED3DSIH_SAMPLE_GRAD,                      "u",    "fRSff"},
    {WINED3D_SM4_OP_SAMPLE_B,                         WINED3DSIH_SAMPLE_B,                         "u",    "fRSf"},
    {WINED3D_SM4_OP_SQRT,                             WINED3DSIH_SQRT,                             "f",    "f"},
    {WINED3D_SM4_OP_SWITCH,                           WINED3DSIH_SWITCH,                           "",     "u"},
    {WINED3D_SM4_OP_SINCOS,                           WINED3DSIH_SINCOS,                           "ff",   "f"},
    {WINED3D_SM4_OP_UDIV,                             WINED3DSIH_UDIV,                             "uu",   "uu"},
    {WINED3D_SM4_OP_ULT,                              WINED3DSIH_ULT,                              "u",    "uu"},
    {WINED3D_SM4_OP_UGE,                              WINED3DSIH_UGE,                              "u",    "uu"},
    {WINED3D_SM4_OP_UMAX,                             WINED3DSIH_UMAX,                             "u",    "uu"},
    {WINED3D_SM4_OP_USHR,                             WINED3DSIH_USHR,                             "u",    "uu"},
    {WINED3D_SM4_OP_UTOF,                             WINED3DSIH_UTOF,                             "f",    "u"},
    {WINED3D_SM4_OP_XOR,                              WINED3DSIH_XOR,                              "u",    "uu"},
    {WINED3D_SM4_OP_DCL_RESOURCE,                     WINED3DSIH_DCL,                              "R",    "",
            shader_sm4_read_dcl_resource},
    {WINED3D_SM4_OP_DCL_CONSTANT_BUFFER,              WINED3DSIH_DCL_CONSTANT_BUFFER,              "",     "",
            shader_sm4_read_dcl_constant_buffer},
    {WINED3D_SM4_OP_DCL_SAMPLER,                      WINED3DSIH_DCL_SAMPLER,                      "",     "",
            shader_sm4_read_dcl_sampler},
    {WINED3D_SM4_OP_DCL_OUTPUT_TOPOLOGY,              WINED3DSIH_DCL_OUTPUT_TOPOLOGY,              "",     "",
            shader_sm4_read_dcl_output_topology},
    {WINED3D_SM4_OP_DCL_INPUT_PRIMITIVE,              WINED3DSIH_DCL_INPUT_PRIMITIVE,              "",     "",
            shader_sm4_read_dcl_input_primitive},
    {WINED3D_SM4_OP_DCL_VERTICES_OUT,                 WINED3DSIH_DCL_VERTICES_OUT,                 "",     "",
            shader_sm4_read_declaration_count},
    {WINED3D_SM4_OP_DCL_INPUT,                        WINED3DSIH_DCL_INPUT,                        "",     "",
            shader_sm4_read_declaration_dst},
    {WINED3D_SM4_OP_DCL_INPUT_SGV,                    WINED3DSIH_DCL_INPUT_SGV,                    "",     "",
            shader_sm4_read_declaration_register_semantic},
    {WINED3D_SM4_OP_DCL_INPUT_SIV,                    WINED3DSIH_DCL_INPUT_SIV,                    "",     "",
            shader_sm4_read_declaration_register_semantic},
    {WINED3D_SM4_OP_DCL_INPUT_PS,                     WINED3DSIH_DCL_INPUT_PS,                     "",     "",
            shader_sm4_read_dcl_input_ps},
    {WINED3D_SM4_OP_DCL_INPUT_PS_SGV,                 WINED3DSIH_DCL_INPUT_PS_SGV,                 "",     "",
            shader_sm4_read_declaration_register_semantic},
    {WINED3D_SM4_OP_DCL_INPUT_PS_SIV,                 WINED3DSIH_DCL_INPUT_PS_SIV,                 "",     "",
            shader_sm4_read_dcl_input_ps_siv},
    {WINED3D_SM4_OP_DCL_OUTPUT,                       WINED3DSIH_DCL_OUTPUT,                       "",     "",
            shader_sm4_read_declaration_dst},
    {WINED3D_SM4_OP_DCL_OUTPUT_SIV,                   WINED3DSIH_DCL_OUTPUT_SIV,                   "",     "",
            shader_sm4_read_declaration_register_semantic},
    {WINED3D_SM4_OP_DCL_TEMPS,                        WINED3DSIH_DCL_TEMPS,                        "",     "",
            shader_sm4_read_declaration_count},
    {WINED3D_SM4_OP_DCL_GLOBAL_FLAGS,                 WINED3DSIH_DCL_GLOBAL_FLAGS,                 "",     "",
            shader_sm4_read_dcl_global_flags},
    {WINED3D_SM4_OP_GATHER4,                          WINED3DSIH_GATHER4,                          "u",    "fRS"},
    {WINED3D_SM5_OP_HS_DECLS,                         WINED3DSIH_HS_DECLS,                         "",     ""},
    {WINED3D_SM5_OP_HS_CONTROL_POINT_PHASE,           WINED3DSIH_HS_CONTROL_POINT_PHASE,           "",     ""},
    {WINED3D_SM5_OP_HS_FORK_PHASE,                    WINED3DSIH_HS_FORK_PHASE,                    "",     ""},
    {WINED3D_SM5_OP_HS_JOIN_PHASE,                    WINED3DSIH_HS_JOIN_PHASE,                    "",     ""},
    {WINED3D_SM5_OP_BUFINFO,                          WINED3DSIH_BUFINFO,                          "i",    "U"},
    {WINED3D_SM5_OP_DERIV_RTX_COARSE,                 WINED3DSIH_DSX_COARSE,                       "f",    "f"},
    {WINED3D_SM5_OP_DERIV_RTX_FINE,                   WINED3DSIH_DSX_FINE,                         "f",    "f"},
    {WINED3D_SM5_OP_DERIV_RTY_COARSE,                 WINED3DSIH_DSY_COARSE,                       "f",    "f"},
    {WINED3D_SM5_OP_DERIV_RTY_FINE,                   WINED3DSIH_DSY_FINE,                         "f",    "f"},
    {WINED3D_SM5_OP_GATHER4_C,                        WINED3DSIH_GATHER4_C,                        "f",    "fRSf"},
    {WINED3D_SM5_OP_BFI,                              WINED3DSIH_BFI,                              "u",    "uuuu"},
    {WINED3D_SM5_OP_DCL_INPUT_CONTROL_POINT_COUNT,    WINED3DSIH_DCL_INPUT_CONTROL_POINT_COUNT,    "",     "",
            shader_sm5_read_control_point_count},
    {WINED3D_SM5_OP_DCL_OUTPUT_CONTROL_POINT_COUNT,   WINED3DSIH_DCL_OUTPUT_CONTROL_POINT_COUNT,   "",     "",
            shader_sm5_read_control_point_count},
    {WINED3D_SM5_OP_DCL_TESSELLATOR_DOMAIN,           WINED3DSIH_DCL_TESSELLATOR_DOMAIN,           "",     "",
            shader_sm5_read_dcl_tessellator_domain},
    {WINED3D_SM5_OP_DCL_TESSELLATOR_PARTITIONING,     WINED3DSIH_DCL_TESSELLATOR_PARTITIONING,     "",     "",
            shader_sm5_read_dcl_tessellator_partitioning},
    {WINED3D_SM5_OP_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, WINED3DSIH_DCL_TESSELLATOR_OUTPUT_PRIMITIVE, "",     "",
            shader_sm5_read_dcl_tessellator_output_primitive},
    {WINED3D_SM5_OP_DCL_HS_MAX_TESSFACTOR,            WINED3DSIH_DCL_HS_MAX_TESSFACTOR,            "",     "",
            shader_sm5_read_dcl_hs_max_tessfactor},
    {WINED3D_SM5_OP_DCL_HS_FORK_PHASE_INSTANCE_COUNT, WINED3DSIH_DCL_HS_FORK_PHASE_INSTANCE_COUNT, "",     "",
            shader_sm4_read_declaration_count},
    {WINED3D_SM5_OP_DCL_UAV_TYPED,                    WINED3DSIH_DCL_UAV_TYPED,                    "",     "",
            shader_sm4_read_dcl_resource},
    {WINED3D_SM5_OP_DCL_RESOURCE_STRUCTURED,          WINED3DSIH_DCL_RESOURCE_STRUCTURED,          "",     "",
            shader_sm5_read_dcl_resource_structured},
    {WINED3D_SM5_OP_STORE_UAV_TYPED,                  WINED3DSIH_STORE_UAV_TYPED,                  "U",    "iu"},
    {WINED3D_SM5_OP_LD_RAW,                           WINED3DSIH_LD_RAW,                           "u",    "iU"},
    {WINED3D_SM5_OP_STORE_RAW,                        WINED3DSIH_STORE_RAW,                        "U",    "iu"},
    {WINED3D_SM5_OP_LD_STRUCTURED,                    WINED3DSIH_LD_STRUCTURED,                    "u",    "uuR"},
    {WINED3D_SM5_OP_STORE_STRUCTURED,                 WINED3DSIH_STORE_STRUCTURED,                 "U",    "iiu"},
    {WINED3D_SM5_OP_IMM_ATOMIC_CONSUME,               WINED3DSIH_IMM_ATOMIC_CONSUME,               "u",    "U"},
};

static const enum wined3d_shader_register_type register_type_table[] =
{
    /* WINED3D_SM4_RT_TEMP */                    WINED3DSPR_TEMP,
    /* WINED3D_SM4_RT_INPUT */                   WINED3DSPR_INPUT,
    /* WINED3D_SM4_RT_OUTPUT */                  WINED3DSPR_OUTPUT,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM4_RT_IMMCONST */                WINED3DSPR_IMMCONST,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM4_RT_SAMPLER */                 WINED3DSPR_SAMPLER,
    /* WINED3D_SM4_RT_RESOURCE */                WINED3DSPR_RESOURCE,
    /* WINED3D_SM4_RT_CONSTBUFFER */             WINED3DSPR_CONSTBUFFER,
    /* WINED3D_SM4_RT_IMMCONSTBUFFER */          WINED3DSPR_IMMCONSTBUFFER,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM4_RT_PRIMID */                  WINED3DSPR_PRIMID,
    /* WINED3D_SM4_RT_DEPTHOUT */                WINED3DSPR_DEPTHOUT,
    /* WINED3D_SM4_RT_NULL */                    WINED3DSPR_NULL,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM5_RT_OUTPUT_CONTROL_POINT_ID */ WINED3DSPR_OUTPOINTID,
    /* WINED3D_SM5_RT_FORK_INSTANCE_ID */        WINED3DSPR_FORKINSTID,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM5_RT_INPUT_CONTROL_POINT */     WINED3DSPR_INCONTROLPOINT,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM5_RT_PATCH_CONSTANT_DATA */     WINED3DSPR_PATCHCONST,
    /* WINED3D_SM5_RT_DOMAIN_LOCATION */         WINED3DSPR_TESSCOORD,
    /* UNKNOWN */                                ~0u,
    /* WINED3D_SM5_RT_UAV */                     WINED3DSPR_UAV,
    /* WINED3D_SM5_RT_SHARED_MEMORY */           WINED3DSPR_GROUPSHAREDMEM,
};

static const struct wined3d_sm4_opcode_info *get_opcode_info(enum wined3d_sm4_opcode opcode)
{
    unsigned int i;

    for (i = 0; i < sizeof(opcode_table) / sizeof(*opcode_table); ++i)
    {
        if (opcode == opcode_table[i].opcode) return &opcode_table[i];
    }

    return NULL;
}

static void map_register(const struct wined3d_sm4_data *priv, struct wined3d_shader_register *reg)
{
    switch (priv->shader_version.type)
    {
        case WINED3D_SHADER_TYPE_PIXEL:
            if (reg->type == WINED3DSPR_OUTPUT)
            {
                unsigned int reg_idx = reg->idx[0].offset;

                if (reg_idx >= ARRAY_SIZE(priv->output_map))
                {
                    ERR("Invalid output index %u.\n", reg_idx);
                    break;
                }

                reg->type = WINED3DSPR_COLOROUT;
                reg->idx[0].offset = priv->output_map[reg_idx];
            }
            break;

        default:
            break;
    }
}

static enum wined3d_data_type map_data_type(char t)
{
    switch (t)
    {
        case 'f':
            return WINED3D_DATA_FLOAT;
        case 'i':
            return WINED3D_DATA_INT;
        case 'u':
            return WINED3D_DATA_UINT;
        case 'R':
            return WINED3D_DATA_RESOURCE;
        case 'S':
            return WINED3D_DATA_SAMPLER;
        case 'U':
            return WINED3D_DATA_UAV;
        default:
            ERR("Invalid data type '%c'.\n", t);
            return WINED3D_DATA_FLOAT;
    }
}

static void *shader_sm4_init(const DWORD *byte_code, const struct wined3d_shader_signature *output_signature)
{
    struct wined3d_sm4_data *priv;
    unsigned int i;

    if (!(priv = HeapAlloc(GetProcessHeap(), 0, sizeof(*priv))))
    {
        ERR("Failed to allocate private data\n");
        return NULL;
    }

    memset(priv->output_map, 0xff, sizeof(priv->output_map));
    for (i = 0; i < output_signature->element_count; ++i)
    {
        struct wined3d_shader_signature_element *e = &output_signature->elements[i];

        if (e->register_idx >= ARRAY_SIZE(priv->output_map))
        {
            WARN("Invalid output index %u.\n", e->register_idx);
            continue;
        }

        priv->output_map[e->register_idx] = e->semantic_idx;
    }

    list_init(&priv->src_free);
    list_init(&priv->src);

    return priv;
}

static void shader_sm4_free(void *data)
{
    struct wined3d_shader_src_param_entry *e1, *e2;
    struct wined3d_sm4_data *priv = data;

    list_move_head(&priv->src_free, &priv->src);
    LIST_FOR_EACH_ENTRY_SAFE(e1, e2, &priv->src_free, struct wined3d_shader_src_param_entry, entry)
    {
        HeapFree(GetProcessHeap(), 0, e1);
    }
    HeapFree(GetProcessHeap(), 0, priv);
}

static struct wined3d_shader_src_param *get_src_param(struct wined3d_sm4_data *priv)
{
    struct wined3d_shader_src_param_entry *e;
    struct list *elem;

    if (!list_empty(&priv->src_free))
    {
        elem = list_head(&priv->src_free);
        list_remove(elem);
    }
    else
    {
        if (!(e = HeapAlloc(GetProcessHeap(), 0, sizeof(*e))))
            return NULL;
        elem = &e->entry;
    }

    list_add_tail(&priv->src, elem);
    e = LIST_ENTRY(elem, struct wined3d_shader_src_param_entry, entry);
    return &e->param;
}

static void shader_sm4_read_header(void *data, const DWORD **ptr, struct wined3d_shader_version *shader_version)
{
    struct wined3d_sm4_data *priv = data;
    DWORD version_token;

    priv->end = *ptr;

    version_token = *(*ptr)++;
    TRACE("Version: 0x%08x.\n", version_token);

    TRACE("Token count: %u.\n", **ptr);
    priv->end += *(*ptr)++;

    switch (version_token >> 16)
    {
        case WINED3D_SM4_PS:
            priv->shader_version.type = WINED3D_SHADER_TYPE_PIXEL;
            break;

        case WINED3D_SM4_VS:
            priv->shader_version.type = WINED3D_SHADER_TYPE_VERTEX;
            break;

        case WINED3D_SM4_GS:
            priv->shader_version.type = WINED3D_SHADER_TYPE_GEOMETRY;
            break;

        case WINED3D_SM5_HS:
            priv->shader_version.type = WINED3D_SHADER_TYPE_HULL;
            break;

        case WINED3D_SM5_DS:
            priv->shader_version.type = WINED3D_SHADER_TYPE_DOMAIN;
            break;

        default:
            FIXME("Unrecognized shader type %#x.\n", version_token >> 16);
    }
    priv->shader_version.major = WINED3D_SM4_VERSION_MAJOR(version_token);
    priv->shader_version.minor = WINED3D_SM4_VERSION_MINOR(version_token);

    *shader_version = priv->shader_version;
}

static BOOL shader_sm4_read_reg_idx(struct wined3d_sm4_data *priv, const DWORD **ptr,
        DWORD addressing, struct wined3d_shader_register_index *reg_idx)
{
    if (addressing & WINED3D_SM4_ADDRESSING_RELATIVE)
    {
        struct wined3d_shader_src_param *rel_addr = get_src_param(priv);

        if (!(reg_idx->rel_addr = rel_addr))
        {
            ERR("Failed to get src param for relative addressing.\n");
            return FALSE;
        }

        if (addressing & WINED3D_SM4_ADDRESSING_OFFSET)
            reg_idx->offset = *(*ptr)++;
        else
            reg_idx->offset = 0;
        shader_sm4_read_src_param(priv, ptr, WINED3D_DATA_INT, rel_addr);
    }
    else
    {
        reg_idx->rel_addr = NULL;
        reg_idx->offset = *(*ptr)++;
    }

    return TRUE;
}

static BOOL shader_sm4_read_param(struct wined3d_sm4_data *priv, const DWORD **ptr,
        enum wined3d_data_type data_type, struct wined3d_shader_register *param,
        enum wined3d_shader_src_modifier *modifier)
{
    enum wined3d_sm4_register_type register_type;
    DWORD token = *(*ptr)++;
    DWORD order;

    register_type = (token & WINED3D_SM4_REGISTER_TYPE_MASK) >> WINED3D_SM4_REGISTER_TYPE_SHIFT;
    if (register_type >= sizeof(register_type_table) / sizeof(*register_type_table)
            || register_type_table[register_type] == ~0u)
    {
        FIXME("Unhandled register type %#x.\n", register_type);
        param->type = WINED3DSPR_TEMP;
    }
    else
    {
        param->type = register_type_table[register_type];
    }
    param->data_type = data_type;

    if (token & WINED3D_SM4_REGISTER_MODIFIER)
    {
        DWORD m = *(*ptr)++;

        switch (m)
        {
            case 0x41:
                *modifier = WINED3DSPSM_NEG;
                break;

            case 0x81:
                *modifier = WINED3DSPSM_ABS;
                break;

            case 0xc1:
                *modifier = WINED3DSPSM_ABSNEG;
                break;

            default:
                FIXME("Skipping modifier 0x%08x.\n", m);
                *modifier = WINED3DSPSM_NONE;
                break;
        }
    }
    else
    {
        *modifier = WINED3DSPSM_NONE;
    }

    order = (token & WINED3D_SM4_REGISTER_ORDER_MASK) >> WINED3D_SM4_REGISTER_ORDER_SHIFT;

    if (order < 1)
        param->idx[0].offset = ~0U;
    else
    {
        DWORD addressing = (token & WINED3D_SM4_ADDRESSING_MASK0) >> WINED3D_SM4_ADDRESSING_SHIFT0;
        if (!(shader_sm4_read_reg_idx(priv, ptr, addressing, &param->idx[0])))
        {
            ERR("Failed to read register index.\n");
            return FALSE;
        }
    }

    if (order < 2)
        param->idx[1].offset = ~0U;
    else
    {
        DWORD addressing = (token & WINED3D_SM4_ADDRESSING_MASK1) >> WINED3D_SM4_ADDRESSING_SHIFT1;
        if (!(shader_sm4_read_reg_idx(priv, ptr, addressing, &param->idx[1])))
        {
            ERR("Failed to read register index.\n");
            return FALSE;
        }
    }

    if (order > 2)
        FIXME("Unhandled order %u.\n", order);

    if (register_type == WINED3D_SM4_RT_IMMCONST)
    {
        enum wined3d_sm4_immconst_type immconst_type =
                (token & WINED3D_SM4_IMMCONST_TYPE_MASK) >> WINED3D_SM4_IMMCONST_TYPE_SHIFT;

        switch (immconst_type)
        {
            case WINED3D_SM4_IMMCONST_SCALAR:
                param->immconst_type = WINED3D_IMMCONST_SCALAR;
                memcpy(param->immconst_data, *ptr, 1 * sizeof(DWORD));
                *ptr += 1;
                break;

            case WINED3D_SM4_IMMCONST_VEC4:
                param->immconst_type = WINED3D_IMMCONST_VEC4;
                memcpy(param->immconst_data, *ptr, 4 * sizeof(DWORD));
                *ptr += 4;
                break;

            default:
                FIXME("Unhandled immediate constant type %#x.\n", immconst_type);
                break;
        }
    }

    map_register(priv, param);

    return TRUE;
}

static BOOL shader_sm4_read_src_param(struct wined3d_sm4_data *priv, const DWORD **ptr,
        enum wined3d_data_type data_type, struct wined3d_shader_src_param *src_param)
{
    DWORD token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, data_type, &src_param->reg, &src_param->modifiers))
    {
        ERR("Failed to read parameter.\n");
        return FALSE;
    }

    if (src_param->reg.type == WINED3DSPR_IMMCONST)
    {
        src_param->swizzle = WINED3DSP_NOSWIZZLE;
    }
    else
    {
        enum wined3d_sm4_swizzle_type swizzle_type =
                (token & WINED3D_SM4_SWIZZLE_TYPE_MASK) >> WINED3D_SM4_SWIZZLE_TYPE_SHIFT;

        switch (swizzle_type)
        {
            case WINED3D_SM4_SWIZZLE_NONE:
                src_param->swizzle = WINED3DSP_NOSWIZZLE;
                break;

            case WINED3D_SM4_SWIZZLE_SCALAR:
                src_param->swizzle = (token & WINED3D_SM4_SWIZZLE_MASK) >> WINED3D_SM4_SWIZZLE_SHIFT;
                src_param->swizzle = (src_param->swizzle & 0x3) * 0x55;
                break;

            case WINED3D_SM4_SWIZZLE_VEC4:
                src_param->swizzle = (token & WINED3D_SM4_SWIZZLE_MASK) >> WINED3D_SM4_SWIZZLE_SHIFT;
                break;

            default:
                FIXME("Unhandled swizzle type %#x.\n", swizzle_type);
                break;
        }
    }

    return TRUE;
}

static BOOL shader_sm4_read_dst_param(struct wined3d_sm4_data *priv, const DWORD **ptr,
        enum wined3d_data_type data_type, struct wined3d_shader_dst_param *dst_param)
{
    enum wined3d_shader_src_modifier modifier;
    DWORD token = **ptr;

    if (!shader_sm4_read_param(priv, ptr, data_type, &dst_param->reg, &modifier))
    {
        ERR("Failed to read parameter.\n");
        return FALSE;
    }

    if (modifier != WINED3DSPSM_NONE)
    {
        ERR("Invalid source modifier %#x on destination register.\n", modifier);
        return FALSE;
    }

    dst_param->write_mask = (token & WINED3D_SM4_WRITEMASK_MASK) >> WINED3D_SM4_WRITEMASK_SHIFT;
    dst_param->modifiers = 0;
    dst_param->shift = 0;

    return TRUE;
}

static void shader_sm4_read_instruction_modifier(DWORD modifier, struct wined3d_shader_instruction *ins)
{
    static const DWORD recognized_bits = WINED3D_SM4_INSTRUCTION_MODIFIER
            | WINED3D_SM4_MODIFIER_AOFFIMMI
            | WINED3D_SM4_AOFFIMMI_U_MASK
            | WINED3D_SM4_AOFFIMMI_V_MASK
            | WINED3D_SM4_AOFFIMMI_W_MASK;

    if (modifier & ~recognized_bits)
    {
        FIXME("Unhandled modifier 0x%08x.\n", modifier);
    }
    else
    {
        /* Bit fields are used for sign extension */
        struct
        {
            int u : 4;
            int v : 4;
            int w : 4;
        }
        aoffimmi;
        aoffimmi.u = (modifier & WINED3D_SM4_AOFFIMMI_U_MASK) >> WINED3D_SM4_AOFFIMMI_U_SHIFT;
        aoffimmi.v = (modifier & WINED3D_SM4_AOFFIMMI_V_MASK) >> WINED3D_SM4_AOFFIMMI_V_SHIFT;
        aoffimmi.w = (modifier & WINED3D_SM4_AOFFIMMI_W_MASK) >> WINED3D_SM4_AOFFIMMI_W_SHIFT;
        ins->texel_offset.u = aoffimmi.u;
        ins->texel_offset.v = aoffimmi.v;
        ins->texel_offset.w = aoffimmi.w;
    }
}

static void shader_sm4_read_instruction(void *data, const DWORD **ptr, struct wined3d_shader_instruction *ins)
{
    const struct wined3d_sm4_opcode_info *opcode_info;
    DWORD opcode_token, opcode, previous_token;
    struct wined3d_sm4_data *priv = data;
    unsigned int i, len;
    const DWORD *p;

    list_move_head(&priv->src_free, &priv->src);

    opcode_token = *(*ptr)++;
    opcode = opcode_token & WINED3D_SM4_OPCODE_MASK;

    len = ((opcode_token & WINED3D_SM4_INSTRUCTION_LENGTH_MASK) >> WINED3D_SM4_INSTRUCTION_LENGTH_SHIFT);
    if (!len)
        len = **ptr;
    --len;

    if (TRACE_ON(d3d_bytecode))
    {
        TRACE_(d3d_bytecode)("[ %08x ", opcode_token);
        for (i = 0; i < len; ++i)
        {
            TRACE_(d3d_bytecode)("%08x ", (*ptr)[i]);
        }
        TRACE_(d3d_bytecode)("]\n");
    }

    if (!(opcode_info = get_opcode_info(opcode)))
    {
        FIXME("Unrecognized opcode %#x, opcode_token 0x%08x.\n", opcode, opcode_token);
        ins->handler_idx = WINED3DSIH_TABLE_SIZE;
        *ptr += len;
        return;
    }

    ins->handler_idx = opcode_info->handler_idx;
    ins->flags = 0;
    ins->coissue = 0;
    ins->predicate = NULL;
    ins->dst_count = strlen(opcode_info->dst_info);
    ins->dst = priv->dst_param;
    ins->src_count = strlen(opcode_info->src_info);
    ins->src = priv->src_param;
    memset(&ins->texel_offset, 0, sizeof(ins->texel_offset));

    p = *ptr;
    *ptr += len;

    previous_token = opcode_token;
    while (previous_token & WINED3D_SM4_INSTRUCTION_MODIFIER)
        shader_sm4_read_instruction_modifier(previous_token = *p++, ins);

    if (opcode_info->read_opcode_func)
    {
        opcode_info->read_opcode_func(ins, opcode, opcode_token, p, len, priv);
    }
    else
    {
        enum wined3d_shader_dst_modifier instruction_dst_modifier = WINED3DSPDM_NONE;

        ins->flags = (opcode_token & WINED3D_SM4_INSTRUCTION_FLAGS_MASK) >> WINED3D_SM4_INSTRUCTION_FLAGS_SHIFT;

        if (ins->flags & WINED3D_SM4_INSTRUCTION_FLAG_SATURATE)
        {
            ins->flags &= ~WINED3D_SM4_INSTRUCTION_FLAG_SATURATE;
            instruction_dst_modifier = WINED3DSPDM_SATURATE;
        }

        for (i = 0; i < ins->dst_count; ++i)
        {
            if (!(shader_sm4_read_dst_param(priv, &p, map_data_type(opcode_info->dst_info[i]), &priv->dst_param[i])))
            {
                ins->handler_idx = WINED3DSIH_TABLE_SIZE;
                return;
            }
            priv->dst_param[i].modifiers |= instruction_dst_modifier;
        }

        for (i = 0; i < ins->src_count; ++i)
        {
            if (!(shader_sm4_read_src_param(priv, &p, map_data_type(opcode_info->src_info[i]), &priv->src_param[i])))
            {
                ins->handler_idx = WINED3DSIH_TABLE_SIZE;
                return;
            }
        }
    }
}

static BOOL shader_sm4_is_end(void *data, const DWORD **ptr)
{
    struct wined3d_sm4_data *priv = data;
    return *ptr == priv->end;
}

const struct wined3d_shader_frontend sm4_shader_frontend =
{
    shader_sm4_init,
    shader_sm4_free,
    shader_sm4_read_header,
    shader_sm4_read_instruction,
    shader_sm4_is_end,
};
