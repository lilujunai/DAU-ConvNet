#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/platform/default/logging.h"
#include "tensorflow/core/framework/shape_inference.h"
//#include "tensorflow/core/util/cuda_launch_config.h"
#include "dau_conv/base_dau_conv_layer.hpp"
#include "dau_conv_layer_tensorflow.hpp"
//using DAUConvNet::DAUConvSettings;
using namespace tensorflow;

using CPUDevice = Eigen::ThreadPoolDevice;
using GPUDevice = Eigen::GpuDevice;

//initialize with w, mu1, mu2, sigma
REGISTER_OP("DAUConv")
        .Input("input: float")
        .Input("weights: float")
        .Input("mu1: float")
        .Input("mu2: float")
        .Input("sigma: float")
        .Output("output: float")
        .Attr("number_units_x : int  = 2")
        .Attr("number_units_y : int = 2")
        .Attr("num_output : int = 64")
        .Attr("kernel_size: int = 9")
        .Attr("pad: int = 4")
        .Attr("stride: int = 1")
        .Attr("unit_normalization: bool = true")
        .Attr("square_unit_normalization: bool = false")
        .Attr("mean_iteration_step: int = 1")
        .Attr("sigma_iteration_step: int = 1")
        .Attr("component_border_bound: int = 4")
        .Attr("sigma_lower_bound: float = 0.3")
        .Attr("merge_iteration_step: int = 0")
        .Attr("merge_threshold: int = 1")
        .Attr("unit_testing: bool = false");
/*.SetShapeFn([](::tensorflow::shape_inference::InferenceContext* c) {
  shape_inference::ShapeHandle input_shape;
  TF_RETURN_IF_ERROR(c->WithRank(c->input(0), 4, &input_shape));

  shape_inference::ShapeHandle weight_shape;
  TF_RETURN_IF_ERROR(c->WithRank(c->input(1), 4, &weight_shape));

  shape_inference::DimensionHandle output_rows = c->Dim(weight_shape, 0);

  shape_inference::DimensionHandle input_rows = c->Dim(input_shape, 0);
  shape_inference::DimensionHandle weight_cols = c->Dim(weight_shape, 1);
  shape_inference::DimensionHandle merged;
  TF_RETURN_IF_ERROR(c->Merge(input_rows, weight_cols, &merged));

  c->set_output(0, c->Matrix(output_rows, 1));
  return Status::OK();
});*/

template <typename Device, typename Dtype>
class DAUConvOp : public OpKernel {
public:
    explicit DAUConvOp(OpKernelConstruction* context) : OpKernel(context) {
        int number_units_x;
        int number_units_y;
        int num_output;
        int kernel_size;
        int pad;
        int stride;
        bool unit_normalization;
        bool square_unit_normalization;
        int mean_iteration_step;
        int sigma_iteration_step;
        int component_border_bound;
        float sigma_lower_bound;
        int merge_iteration_step;
        int merge_threshold;
        OP_REQUIRES_OK(context, context->GetAttr("number_units_x", &number_units_x));
        OP_REQUIRES_OK(context, context->GetAttr("number_units_y", &number_units_y));
        OP_REQUIRES_OK(context, context->GetAttr("num_output", &num_output));
        OP_REQUIRES_OK(context, context->GetAttr("kernel_size", &kernel_size));
        OP_REQUIRES_OK(context, context->GetAttr("pad", &pad));
        OP_REQUIRES_OK(context, context->GetAttr("stride", &stride));
        OP_REQUIRES_OK(context, context->GetAttr("unit_normalization", &unit_normalization));
        OP_REQUIRES_OK(context, context->GetAttr("square_unit_normalization", &square_unit_normalization));
        OP_REQUIRES_OK(context, context->GetAttr("mean_iteration_step", &mean_iteration_step));
        OP_REQUIRES_OK(context, context->GetAttr("sigma_iteration_step", &sigma_iteration_step));
        OP_REQUIRES_OK(context, context->GetAttr("component_border_bound", &component_border_bound));
        OP_REQUIRES_OK(context, context->GetAttr("sigma_lower_bound", &sigma_lower_bound));
        OP_REQUIRES_OK(context, context->GetAttr("merge_iteration_step", &merge_iteration_step));
        OP_REQUIRES_OK(context, context->GetAttr("merge_threshold", &merge_threshold));
        OP_REQUIRES_OK(context, context->GetAttr("unit_testing", &this->unit_testing));
        dau_conv_settings.offsets_already_centered = true;
        dau_conv_settings.num_output = num_output;
        //num units per X and per Y
        dau_conv_settings.number_units.push_back(number_units_x);
        dau_conv_settings.number_units.push_back(number_units_y);
        //bias handled by Tensorflow
        dau_conv_settings.bias_term = false;
        dau_conv_settings.kernel_size = kernel_size;
        dau_conv_settings.pad = pad;
        dau_conv_settings.stride = stride;
        dau_conv_settings.unit_normalization = unit_normalization;
        dau_conv_settings.square_unit_normalization = square_unit_normalization;
        dau_conv_settings.mean_iteration_step = mean_iteration_step;
        dau_conv_settings.sigma_iteration_step = sigma_iteration_step;
        dau_conv_settings.component_border_bound = component_border_bound;
        dau_conv_settings.sigma_lower_bound = sigma_lower_bound;
        dau_conv_settings.merge_iteration_step = merge_iteration_step;
        dau_conv_settings.merge_threshold = merge_threshold;

    }

    void Compute(OpKernelContext* context) override {

        DCHECK_EQ(5, context->num_inputs());

        // in_train is used only for merge_iteration_step, which is not setup.
        /*
        AllocatorAttributes alloc_attrs;
        tensorflow::DeviceBase* device = context->device();
        Allocator* allocator = context->device()->GetAllocator(alloc_attrs);
        AllocatorStats stats;
        allocator->GetStats(&stats);
        printf("Bytes in use %d\n",stats.bytes_in_use);
        */

        bool in_train = false;

        const Tensor* input;
        const Tensor* weights;
        const Tensor* mu1;
        const Tensor* mu2;
        const Tensor* sigma;

        context->input("input", &input);
        context->input("weights",&weights);
        context->input("mu1",&mu1);
        context->input("mu2",&mu2);
        context->input("sigma",&sigma);


        const TensorShape& input_shape = input->shape();
        const TensorShape& weights_shape = weights->shape();


        //Check if output size of parameters equals to specified number of outputs
        DCHECK_EQ(dau_conv_settings.num_output, weights_shape.dim_size(weights_shape.dims()-1));
        DCHECK_EQ(dau_conv_settings.num_output, mu1->shape().dim_size(mu1->shape().dims()-1));
        DCHECK_EQ(dau_conv_settings.num_output, mu2->shape().dim_size(mu2->shape().dims()-1));
        //DCHECK_EQ(dau_conv_settings.num_output, sigma->shape().dim_size(sigma->shape().dims()-1));


        //Initializer does nothing, input values were of type Filler in caffe
        // tensorflow variables are initialized in python.
        NullDAUComponentInitializerTensorflow<Dtype> param_initializer;

        //DAUConvNet::DAUConvSettings dau_conv_settings;
        DAUKernelComputeTFGPU<Dtype> dau_kernel_compute(context);
        DAUKernelParamsTFGPU<Dtype> dau_kernel_params(context);
        DAUKernelOutputTFGPU<Dtype> dau_kernel_output(context);
        //dau_kernel_params.initialize_params(param_w, param_mu1, param_mu2, param_sigma);

        std::vector<int> bottom_shape;
        for(int i = 0; i < input_shape.dims(); i++){
            bottom_shape.push_back(input_shape.dim_size(i));
        }

        cublasHandle_t handle;
        cublasCreate(&handle);
        /*
        const cudaStream_t* stream = CHECK_NOTNULL(reinterpret_cast<const cudaStream_t*>(context->op_device_context()
                                                                                        -> stream()->implementation()
                                                                                        ->CudaStreamMemberHack()) );
        cublasSetStream(handle, (cudaStream_t) *stream);
         */
        //TODO Get stream from context and add it to handle..


        DAUConvLayerTensorflowGPU<Dtype> tf_layer(handle,context);

        tf_layer.enable_forward(true);
        tf_layer.enable_backward(false);

        tf_layer.InitializeFromInput(dau_conv_settings, (Tensor*) weights,(Tensor*) mu1,(Tensor*) mu2,(Tensor*) sigma);


        tf_layer.LayerSetUp(dau_conv_settings, param_initializer, &dau_kernel_compute, &dau_kernel_params, &dau_kernel_output, bottom_shape, in_train);

        //TensorShape top_tensor_shape({input_shape.dim_size(0), weight_shape.dim_size(1), input_shape.dim_size(2), input_shape.dim_size(3)});
        std::vector<int> top_shape;

        top_shape.push_back(input_shape.dim_size(0));
        top_shape.push_back(dau_conv_settings.num_output);
        top_shape.push_back(input_shape.dim_size(2));
        top_shape.push_back(input_shape.dim_size(3));


        std::vector<int> new_shape = tf_layer.Reshape(bottom_shape, top_shape);


        TensorShape output_shape;
        for(int i = 0; i< top_shape.size(); i++) output_shape.AddDim(top_shape[i]);


        Tensor* output;
        OP_REQUIRES_OK(context, context->allocate_output(0, output_shape, &output));


        auto out_data = output->flat<Dtype>();
        Dtype* top_data = reinterpret_cast<Dtype*>(out_data.data());

        auto input_data = input->flat<Dtype>();
        const Dtype* bottom_data = reinterpret_cast<const Dtype*>(input_data.data());


        tf_layer.Forward_gpu(bottom_data, bottom_shape, top_data, top_shape);

    }
private:
    DAUConvNet::DAUConvSettings dau_conv_settings;
    bool unit_testing;
};

#define REGISTER_CPU(T) REGISTER_KERNEL_BUILDER(Name("BaseOp").Device(DEVICE_CPU), BaseOpOp<CPUDevice, T>);

//REGISTER_CPU(float);
//REGISTER_CPU(int32);

#ifdef GOOGLE_CUDA
#define REGISTER_GPU(T) \
REGISTER_KERNEL_BUILDER(Name("DAUConv").Device(DEVICE_GPU), DAUConvOp<GPUDevice, T>);

//type constraint is for constraining named attributes like "T"
//REGISTER_KERNEL_BUILDER(Name("BaseOp").Device(DEVICE_CPU).TypeConstraint<T>("T"), BaseOpOp<GPUDevice, T>);


REGISTER_GPU(float);
//REGISTER_GPU(int32);
#endif //google_cuda