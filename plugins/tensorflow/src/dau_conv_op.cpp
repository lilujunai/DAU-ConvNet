#include "tensorflow/core/framework/op_kernel.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/platform/default/logging.h"
#include "tensorflow/core/framework/shape_inference.h"
//#include "tensorflow/core/util/cuda_launch_config.h"
#include "dau_conv/base_dau_conv_layer.hpp"
#include "dau_conv_layer_tensorflow.hpp"

#include "dau_conv/util/math_functions.hpp"

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
        .Attr("number_units_ignore : int = 0")
        .Attr("num_output : int = 64")
        .Attr("kernel_size: int = 9")
        .Attr("pad: int = 4")
        .Attr("stride: int = 1")
        .Attr("unit_normalization: bool = true")
        .Attr("square_unit_normalization: bool = false")
        .Attr("mean_iteration_step: int = 1")
        .Attr("sigma_iteration_step: int = 1")
        .Attr("component_border_bound: float = 1")
        .Attr("sigma_lower_bound: float = 0.3")
        .Attr("merge_iteration_step: int = 0")
        .Attr("merge_threshold: int = 1")
        .Attr("unit_testing: bool = false")
        .Attr("mu_learning_rate_factor: float = 1.0")
.SetShapeFn([](::tensorflow::shape_inference::InferenceContext* c) {
  shape_inference::ShapeHandle input_shape;
  TF_RETURN_IF_ERROR(c->WithRank(c->input(0), 4, &input_shape));
  // TODO: check input sizes for w,mu1,mu2
    //

    shape_inference::ShapeHandle weight_shape;
    TF_RETURN_IF_ERROR(c->WithRank(c->input(1), 4, &weight_shape));
    shape_inference::ShapeHandle mu1_shape;
    TF_RETURN_IF_ERROR(c->WithRank(c->input(2), 4, &mu1_shape));
    shape_inference::ShapeHandle mu2_shape;
    TF_RETURN_IF_ERROR(c->WithRank(c->input(3), 4, &mu2_shape));
    shape_inference::ShapeHandle sigma_shape;
    TF_RETURN_IF_ERROR(c->WithRank(c->input(4), 4, &sigma_shape));

    int num_out;
    c->GetAttr("num_output", &num_out);
    shape_inference::DimensionHandle output_rows = c->Dim(weight_shape, 3);
    shape_inference::DimensionHandle mu1_out = c->Dim(mu1_shape, 3);
    shape_inference::DimensionHandle mu2_out = c->Dim(mu2_shape, 3);
    shape_inference::DimensionHandle sigma_out = c->Dim(sigma_shape, 3);
    shape_inference::DimensionHandle out_dim;

    //check if the number of outputs is set correctly in input tensors
    TF_RETURN_IF_ERROR(c->WithValue(output_rows, num_out, &out_dim));
    TF_RETURN_IF_ERROR(c->WithValue(mu1_out, num_out, &out_dim));
    TF_RETURN_IF_ERROR(c->WithValue(mu2_out, num_out, &out_dim));
    TF_RETURN_IF_ERROR(c->WithValue(sigma_out, num_out, &out_dim));

shape_inference::ShapeHandle output_shape;

  TF_RETURN_IF_ERROR(c->ReplaceDim(input_shape, 1, output_rows, &output_shape));

  c->set_output(0, output_shape);
  return Status::OK();
});

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
        float component_border_bound;
        float sigma_lower_bound;
        int merge_iteration_step;
        int merge_threshold;
        OP_REQUIRES_OK(context, context->GetAttr("number_units_x", &number_units_x));
        OP_REQUIRES_OK(context, context->GetAttr("number_units_y", &number_units_y));
        OP_REQUIRES_OK(context, context->GetAttr("number_units_ignore", &this->number_units_ignore));
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

        DAUConvNet::DAUConvSettings dau_conv_settings_ = this->dau_conv_settings;

        const TensorShape input_shape = input->shape();
        const TensorShape weights_shape = weights->shape();
        const TensorShape mu1_shape = mu1->shape();
        const TensorShape mu2_shape = mu2->shape();
        const TensorShape sigma_shape = sigma->shape();

        std::vector<int> bottom_shape;
        std::vector<int> top_shape;

        // define bottom shape (from input)
        for(int i = 0; i < input_shape.dims(); i++)
            bottom_shape.push_back(input_shape.dim_size(i));

        // define top shape (for output)
        top_shape.push_back(input_shape.dim_size(0));
        top_shape.push_back(dau_conv_settings_.num_output);
        top_shape.push_back(input_shape.dim_size(2));
        top_shape.push_back(input_shape.dim_size(3));

        TensorShape output_shape;
        for(int i = 0; i< top_shape.size(); i++)
            output_shape.AddDim(top_shape[i]);

        Tensor* output;
        OP_REQUIRES_OK(context, context->allocate_output(0, output_shape, &output));

        //Check if output size of parameters equals to specified number of outputs
        //now checked in shape inference
        //DCHECK_EQ(dau_conv_settings.num_output, weights_shape.dim_size(weights_shape.dims()-1));
        //DCHECK_EQ(dau_conv_settings.num_output, mu1_shape.dim_size(mu1_shape.dims()-1));
        //DCHECK_EQ(dau_conv_settings.num_output, mu2_shape.dim_size(mu2_shape.dims()-1));
        //DCHECK_EQ(dau_conv_settings.num_output, sigma_shape.dim_size(sigma_shape.dims()-1));


        // Initializer does nothing; tensorflow variables are initialized in python.

        NullDAUComponentInitializerTensorflow<Dtype> param_initializer;
        DAUKernelComputeTFGPU<Dtype> dau_kernel_compute(context);
        DAUKernelParamsTFGPU<Dtype> dau_kernel_params(context);
        DAUKernelOutputTFGPU<Dtype> dau_kernel_output(context);


        cublasHandle_t handle;
        cublasCreate(&handle);
        /*
        const cudaStream_t* stream = CHECK_NOTNULL(reinterpret_cast<const cudaStream_t*>(context->op_device_context()
                                                                                        -> stream()->implementation()
                                                                                        ->CudaStreamMemberHack()) );
        cublasSetStream(handle, (cudaStream_t) *stream);
         */
        //TODO Get stream from context and add it to handle..
        {
            Dtype max_mu1 = 0, max_mu2 = 0;

            auto mu1__ = mu1->flat<Dtype>();
            int param_size = mu1__.size();

            DAUConvNet::caffe_gpu_amax(param_size, TENSOR_DATA_PTR_CONST(mu1,Dtype), &max_mu1, handle);
            DAUConvNet::caffe_gpu_amax(param_size, TENSOR_DATA_PTR_CONST(mu2,Dtype), &max_mu2, handle);

            Dtype actual_max_offset = std::max<Dtype>(std::abs(max_mu1), std::abs(max_mu2));

            if (actual_max_offset <= 4) {
                dau_conv_settings_.kernel_size = 2 * 4 + 1;
            } else if (actual_max_offset <= 8) {
                dau_conv_settings_.kernel_size = 2 * 8 + 1;
            } else if (actual_max_offset <= 16) {
                dau_conv_settings_.kernel_size = 2 * 16 + 1;
            } else if (actual_max_offset <= 32) {
                dau_conv_settings_.kernel_size = 2 * 32 + 1;
            } else {
                // OP_REQUIRES_OK will return so we need to release cublas handle
                cublasDestroy(handle);

                OP_REQUIRES_OK(context, Status(tensorflow::error::Code::INVALID_ARGUMENT ,
                                            "DAUConvOp ERROR: actual offsets larger then what CUDA memory allows (setup max_kernel_size and dau_unit_border_bound correctly to avoid this)!!"));
            }


            dau_conv_settings_.pad = (dau_conv_settings_.kernel_size-1)/2;

            if (actual_max_offset != actual_max_offset) {
                // OP_REQUIRES_OK will return so we need to release cublas handle
                cublasDestroy(handle);

                OP_REQUIRES_OK(context, Status(tensorflow::error::Code::FAILED_PRECONDITION,
                                               "DAUConvOp ERROR: got NaN value in offset (mu1,mu2) variable"));
            }

        }
        try {
            DAUConvLayerTensorflowGPU<Dtype> tf_layer(handle,context);

            tf_layer.enable_forward(true);
            tf_layer.enable_backward(false);

            // prevent display of allocation size on each call (except when doing unit testing)
            tf_layer.enable_memalloc_info(this->unit_testing == true ? true : false);

            tf_layer.InitializeFromInput(dau_conv_settings_, (Tensor*) weights,(Tensor*) mu1,(Tensor*) mu2,(Tensor*) sigma);

            tf_layer.LayerSetUp(dau_conv_settings_, param_initializer, &dau_kernel_compute, &dau_kernel_params, &dau_kernel_output, bottom_shape, number_units_ignore, in_train);

            vector<int> new_top_data = tf_layer.Reshape(bottom_shape, top_shape);

            CHECK(new_top_data[0] == top_shape[0]);
            CHECK(new_top_data[1] == top_shape[1]);
            CHECK(new_top_data[2] == top_shape[2]);
            CHECK(new_top_data[3] == top_shape[3]);

            Dtype* top_data = TENSOR_DATA_PTR(output, Dtype);

            const Dtype* bottom_data = TENSOR_DATA_PTR_CONST(input, Dtype);

            // since we can use smaller kernel size of all offsets will fit we need to update max kernel size that is
            // used to clip offsets within this bound
            tf_layer.set_max_kernel_size(dau_conv_settings.kernel_size,dau_conv_settings.kernel_size);

            tf_layer.Forward_gpu(bottom_data, bottom_shape, top_data, top_shape);

        } catch (const DAUExceptionTF& ex) {
            std::cout << "ERROR: got TENSORFLOW status error in DAUConvOp" << std::endl;

        } catch (const DAUException& ex) {
            std::cout << "ERROR: got DAUException error in DAUConvOp" << std::endl;

            // report message to tensorflow
            context->CtxFailureWithWarning(Status(tensorflow::error::Code::INTERNAL, ex.what()));
        }
        //destroy cublas handle after end of op
        cublasDestroy(handle);
    }
private:
    DAUConvNet::DAUConvSettings dau_conv_settings;
    bool unit_testing;
    int number_units_ignore;
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