// dain implemented with ncnn library

#include "dain_ops.h"

#include "opticalflowwarp.comp.hex.h"
#include "opticalflowwarp_pack4.comp.hex.h"

using namespace ncnn;

OpticalFlowWarp::OpticalFlowWarp()
{
    support_vulkan = true;

    pipeline_opticalflowwarp = 0;
    pipeline_opticalflowwarp_pack4 = 0;
}

int OpticalFlowWarp::create_pipeline(const Option& opt)
{
    std::vector<vk_specialization_type> specializations(0 + 0);

    // pack1
    {
        static std::vector<uint32_t> spirv;
        static ncnn::Mutex lock;
        {
            ncnn::MutexLockGuard guard(lock);
            if (spirv.empty())
            {
                compile_spirv_module(opticalflowwarp_comp_data, sizeof(opticalflowwarp_comp_data), opt, spirv);
            }
        }

        pipeline_opticalflowwarp = new Pipeline(vkdev);
        pipeline_opticalflowwarp->set_optimal_local_size_xyz();
        pipeline_opticalflowwarp->create(spirv.data(), spirv.size() * 4, specializations);
    }

    // pack4
    {
        static std::vector<uint32_t> spirv;
        static ncnn::Mutex lock;
        {
            ncnn::MutexLockGuard guard(lock);
            if (spirv.empty())
            {
                compile_spirv_module(opticalflowwarp_pack4_comp_data, sizeof(opticalflowwarp_pack4_comp_data), opt, spirv);
            }
        }

        pipeline_opticalflowwarp_pack4 = new Pipeline(vkdev);
        pipeline_opticalflowwarp_pack4->set_optimal_local_size_xyz();
        pipeline_opticalflowwarp_pack4->create(spirv.data(), spirv.size() * 4, specializations);
    }

    return 0;
}

int OpticalFlowWarp::destroy_pipeline(const Option& opt)
{
    delete pipeline_opticalflowwarp;
    pipeline_opticalflowwarp = 0;

    delete pipeline_opticalflowwarp_pack4;
    pipeline_opticalflowwarp_pack4 = 0;

    return 0;
}

int OpticalFlowWarp::forward(const std::vector<Mat>& bottom_blobs, std::vector<Mat>& top_blobs, const Option& opt) const
{
    const Mat& image_blob = bottom_blobs[0];
    const Mat& flow_blob = bottom_blobs[1];

    int w = image_blob.w;
    int h = image_blob.h;
    int channels = image_blob.c;

    Mat& top_blob = top_blobs[0];
    top_blob.create(w, h, channels);
    if (top_blob.empty())
        return -100;

    #pragma omp parallel for
    for (int q = 0; q < channels; q++)
    {
        float* outptr = top_blob.channel(q);

        const Mat image = image_blob.channel(q);

        const float* fxptr = flow_blob.channel(0);
        const float* fyptr = flow_blob.channel(1);

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                float flow_x = fxptr[0];
                float flow_y = fyptr[0];

                float sample_x = x + flow_x;
                float sample_y = y + flow_y;

                // bilinear interpolate
                float v;
                {
                    int x0 = floor(sample_x);
                    int y0 = floor(sample_y);

                    if (x0 < 0 || y0 < 0 || x0 >= w - 1 || y0 >= h - 1)
                    {
                        v = 0.f;
                    }
                    else
                    {
                        float alpha = sample_x - x0;
                        float beta = sample_y - y0;

                        float v0 = image.row(y0)[x0];
                        float v1 = image.row(y0)[x0 + 1];
                        float v2 = image.row(y0 + 1)[x0];
                        float v3 = image.row(y0 + 1)[x0 + 1];

                        float v4 = v0 * (1 - alpha) + v1 * alpha;
                        float v5 = v2 * (1 - alpha) + v3 * alpha;

                        v = v4 * (1 - beta) + v5 * beta;
                    }
                }

                outptr[0] = v;

                outptr += 1;

                fxptr += 1;
                fyptr += 1;
            }
        }
    }

    return 0;
}

int OpticalFlowWarp::forward(const std::vector<VkMat>& bottom_blobs, std::vector<VkMat>& top_blobs, VkCompute& cmd, const Option& opt) const
{
    const VkMat& image_blob = bottom_blobs[0];
    const VkMat& flow_blob = bottom_blobs[1];

    int w = image_blob.w;
    int h = image_blob.h;
    int channels = image_blob.c;
    size_t elemsize = image_blob.elemsize;
    int elempack = image_blob.elempack;

    VkMat& top_blob = top_blobs[0];
    top_blob.create(w, h, channels, elemsize, elempack, opt.blob_vkallocator);
    if (top_blob.empty())
        return -100;

    std::vector<VkMat> bindings(3);
    bindings[0] = image_blob;
    bindings[1] = flow_blob;
    bindings[2] = top_blob;

    std::vector<vk_constant_type> constants(4);
    constants[0].i = top_blob.w;
    constants[1].i = top_blob.h;
    constants[2].i = top_blob.c;
    constants[3].i = top_blob.cstep;

    if (elempack == 4)
    {
        cmd.record_pipeline(pipeline_opticalflowwarp_pack4, bindings, constants, top_blob);
    }
    else // if (elempack == 1)
    {
        cmd.record_pipeline(pipeline_opticalflowwarp, bindings, constants, top_blob);
    }

    return 0;
}
