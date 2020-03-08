#include <image/Decoder.h>
#include <png.h>
#include <turbojpeg.h>
#include <webp/decode.h>

using namespace reckoning;
using namespace reckoning::image;

enum Format { Format_JPEG, Format_PNG, Format_WEBP, Format_Invalid };

static inline Format guessFormat(const std::shared_ptr<buffer::Buffer>& data)
{
    if (data->size() >= 4 && memcmp(data->data(), "\211PNG", 4) == 0)
        return Format_PNG;
    if (data->size() >= 10 && memcmp(data->data() + 6, "JFIF", 4) == 0)
        return Format_JPEG;
    if (data->size() >= 16 && memcmp(data->data() + 8, "WEBPVP8", 7) == 0)
        return Format_WEBP;
    return Format_Invalid;
}

static inline Decoder::Image decodePNG(const std::shared_ptr<buffer::Buffer>& data)
{
    auto png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        return {};
    }
    auto info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, nullptr, nullptr);
        return {};
    }

    Decoder::Image image;

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return {};
    }
    struct PngData {
        const std::shared_ptr<buffer::Buffer>& data;
        size_t read;
    } pngData = { data, 0 };
    png_set_read_fn(png_ptr, &pngData, [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) -> void {
        png_voidp io_ptr = png_get_io_ptr(png_ptr);
        PngData* data = static_cast<PngData*>(io_ptr);
        // should handle out of bounds reads here
        memcpy(outBytes, data->data->data() + data->read, byteCountToRead);
        data->read += byteCountToRead;
    });
    png_set_sig_bytes(png_ptr, 0);
    png_read_info(png_ptr, info_ptr);

    image.width = png_get_image_width(png_ptr, info_ptr);
    image.height = png_get_image_height(png_ptr, info_ptr);

    const auto bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    const auto color_type = png_get_color_type(png_ptr, info_ptr);

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_RGB
        || color_type == PNG_COLOR_TYPE_GRAY
        || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    switch (color_type) {
    case 4:
    case 6:
        image.alpha = true;
        break;
    default:
        image.alpha = false;
        break;
    }

    image.depth = 32;

    png_read_update_info(png_ptr, info_ptr);
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return {};
    }

    const auto rowBytes = png_get_rowbytes(png_ptr, info_ptr);
    png_bytep* row_pointers = static_cast<png_bytep*>(png_malloc(png_ptr, image.height * sizeof(png_bytep)));
    for (int y = 0; y < image.height; ++y)
        row_pointers[y] = static_cast<png_bytep>(png_malloc(png_ptr, rowBytes));

    png_read_image(png_ptr, row_pointers);

    image.data = buffer::Buffer::create(image.height * rowBytes);
    image.data->setSize(0); // append below will increase our size
    for (int y = 0; y < image.height; ++y)
        image.data->append(row_pointers[y], rowBytes);

    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

    image.bpl = rowBytes;

    return image;
}

static inline Decoder::Image decodeWEBP(const std::shared_ptr<buffer::Buffer>& data)
{
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data->data(), data->size(), &features) != VP8_STATUS_OK) {
        return {};
    }
    Decoder::Image image;
    image.width = features.width;
    image.bpl = features.width * 4;
    image.height = features.height;
    image.alpha = features.has_alpha;
    image.depth = 32;

    image.data = buffer::Buffer::create(image.width * image.height * 4);
    auto out = WebPDecodeRGBAInto(data->data(), data->size(), image.data->data(), image.data->size(), image.width * 4);
    if (out == nullptr) {
        return {};
    }

    return image;
}

static inline Decoder::Image decodeJPEG(const std::shared_ptr<buffer::Buffer>& data)
{
    auto handle = tjInitDecompress();
    int width, height;
    auto bytes = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data->data()));
    if (tjDecompressHeader(handle, bytes, data->size(), &width, &height) != 0) {
        tjDestroy(handle);
        return {};
    }

    Decoder::Image image;
    image.width = width;
    image.bpl = width * 4;
    image.height = height;
    image.alpha = false;
    image.depth = 32;

    image.data = buffer::Buffer::create(image.width * image.height * 4);

    if (tjDecompress2(handle, bytes, data->size(), image.data->data(), width, 0, height, TJPF_RGBA, TJFLAG_FASTDCT) != 0) {
        tjDestroy(handle);
        return {};
    }

    tjDestroy(handle);

    return image;
}

Decoder::Decoder()
{
    mThread = std::thread([&]() {
        for (;;) {
            std::vector<std::shared_ptr<Job > > jobs;
            {
                std::unique_lock<std::mutex> locker(mMutex);
                while (mJobs.empty() && !mStopped) {
                    mCond.wait(locker);
                }
                jobs = std::move(mJobs);
                if (mStopped)
                    return;
            }
            for (const auto& job : jobs) {
                const auto& buf = job->data;
                switch (guessFormat(buf)) {
                case Format_JPEG:
                    job->then.resolve(decodeJPEG(buf));
                    break;
                case Format_PNG:
                    job->then.resolve(decodePNG(buf));
                    break;
                case Format_WEBP:
                    job->then.resolve(decodeWEBP(buf));
                    break;
                default:
                    job->then.resolve({});
                    break;
                }
            }
        }
    });
}

Decoder::~Decoder()
{
    {
        std::unique_lock<std::mutex> locker(mMutex);
        mStopped = true;
        mCond.notify_one();
    }

    mThread.join();
}
