/* Offline G76 compiler check. No GPU execution or iOS acceleration is tested. */
#include <stdio.h>
#include "nir_builder.h"
#include "compiler/glsl_types.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/model/pan_model.h"
static unsigned slots(const struct glsl_type *type, bool bindless)
{
    (void)bindless;
    return glsl_count_attribute_slots(type, false);
}
int main(void)
{
    const uint64_t gpu=0x72110000;
    if(pan_arch(gpu)!=7)return 2;
    glsl_type_singleton_init_or_ref();
    const nir_shader_compiler_options *options=
        pan_get_nir_shader_compiler_options(7,MESA_SHADER_FRAGMENT,false);
    nir_builder builder=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,options,"begonia_constant_colour");
    nir_variable *colour=nir_create_variable_with_location(builder.shader,
        nir_var_shader_out,FRAG_RESULT_DATA0,glsl_vec4_type());
    colour->data.driver_location=0;
    nir_store_var(&builder,colour,nir_imm_vec4(&builder,0.125,0.25,0.5,1.0),15);
    nir_shader_gather_info(builder.shader,nir_shader_get_entrypoint(builder.shader));
    pan_preprocess_nir(builder.shader,gpu);
    NIR_PASS(_,builder.shader,nir_lower_io,nir_var_shader_in|nir_var_shader_out,
        slots,nir_lower_io_use_interpolated_input_intrinsics);
    NIR_PASS(_,builder.shader,nir_opt_constant_folding);
    struct pan_compile_inputs input={.gpu_id=gpu};
    struct pan_shader_info info;
    pan_postprocess_nir(builder.shader,&input,&info);
    struct util_dynarray binary=UTIL_DYNARRAY_INIT;
    pan_shader_compile(builder.shader,&input,&binary,&info);
    if(!binary.size || info.stats.isa!=PAN_STAT_BIFROST) {
        fprintf(stderr,"Wrong compiler architecture or empty binary\n");return 3;
    }
    printf("G76 Bifrost fragment shader: %u bytes; compiler check passed (not hardware execution)\n",(unsigned)binary.size);
    pan_disassemble(stdout,binary.data,binary.size,gpu,false);
    util_dynarray_fini(&binary);
    ralloc_free(builder.shader);
    glsl_type_singleton_decref();
    return 0;
}
