#include "dau_conv/dau_conv_impl/dau_conv_backward_core.hpp"

namespace  DAUConvNet {

void DAUConv_backward_multi_subfeatures_patch_8x32(int IMG_PATCH_SIZE_W, int IMG_PATCH_SIZE_H, int MAX_OFFSET,
                                                   bool SMALLER_WARP_AND_GROUP_K, int BATCH_IMAGES,
                                                   bool USE_INTERPOLATION, bool SINGLE_SUBFEATURE,
                                                   DAUConvBackward<float>::CUDAParams &PARAMS){

    RUN_KERNEL_R5(DAUConvBackwardCUDA, 8, 32, MAX_OFFSET, SMALLER_WARP_AND_GROUP_K, BATCH_IMAGES, USE_INTERPOLATION, SINGLE_SUBFEATURE, PARAMS);
}

}