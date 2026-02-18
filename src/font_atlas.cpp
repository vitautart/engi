#include <cassert>
#include <cstdint>
#include <print>
#include <vector>
#include <cstring>
#include <fstream>

#include "font_atlas.hpp"
#include "rendercommon.hpp"
#include "rendering.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

template <typename Func>
int stbtt_BakeFontBitmap(unsigned char* data, int offset,
                         float pixel_height,
                         unsigned char* pixels, int pw, int ph,
                         int first_char, int num_chars,
                         const Func& fetchGlyphInfo)
{
    float scale;
    int x, y, bottom_y, i;
    stbtt_fontinfo f;
    f.userdata = nullptr;
    if (!stbtt_InitFont(&f, data, offset))
    {
        std::println("[ERROR] Font can't be initialized.");
        return -1;
    }
    STBTT_memset(pixels, 0, pw * ph);
    x = y = 1;
    bottom_y = 1;

    scale = stbtt_ScaleForPixelHeight(&f, pixel_height);

    for (i = 0; i < num_chars; ++i)
    {
        int codepoint = first_char + i;
        int advance, lsb, x0, y0, x1, y1, gw, gh;
        int g = stbtt_FindGlyphIndex(&f, codepoint);
        stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&f, g, scale, scale, &x0, &y0, &x1, &y1);
        gw = x1 - x0;
        gh = y1 - y0;
        if (x + gw + 1 >= pw)
            y = bottom_y, x = 1;
        if (y + gh + 1 >= ph)
            return -i;
        STBTT_assert(x + gw < pw);
        STBTT_assert(y + gh < ph);
        stbtt_MakeGlyphBitmap(&f, pixels + x + y * pw, gw, gh, pw, scale, scale, g);
        fetchGlyphInfo(codepoint, x0, y0, x1, y1, x, y, x + gw, y + gh, static_cast<int>(scale * advance));
        x = x + gw + 1;
        if (y + gh + 1 > bottom_y)
            bottom_y = y + gh + 1;
    }
    return bottom_y;
}

namespace engi::vk
{
    auto FontMonoAtlas::create(
        VkCommandBuffer cmd,
        const std::filesystem::path& font_path,
        int line_height,
        uint32_t bitmap_width,
        uint32_t bitmap_height
    ) -> std::expected<FontMonoAtlas, VkResult>
    {
        Buffer staging_buffer;
        auto res = create(cmd, font_path, line_height, bitmap_width, bitmap_height, staging_buffer);
        if (!res)
        {
            return std::unexpected(res.error());
        }
        delete_later(std::move(staging_buffer), current_frame_id());
        return res;
    }

    auto FontMonoAtlas::create(
        VkCommandBuffer cmd,
        const std::filesystem::path& font_path,
        int line_height,
        uint32_t bitmap_width,
        uint32_t bitmap_height,
        Buffer& staging_buffer
    ) -> std::expected<FontMonoAtlas, VkResult>
    {
        FontMonoAtlas out;
        out.m_line_height = line_height;

        // Read font file
        std::ifstream file(font_path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::println("[ERROR] Failed to open font file: {}", font_path.string());
            return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
        }

        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<unsigned char> font_data(file_size);
        if (!file.read(reinterpret_cast<char*>(font_data.data()), file_size))
        {
            std::println("[ERROR] Failed to read font file");
            return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
        }

        // Create staging buffer for bitmap
        VkBufferCreateInfo staging_buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = bitmap_width * bitmap_height,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        auto staging_buffer_res = Buffer::create_cpu(staging_buffer_info);
        if (!staging_buffer_res)
        {
            std::println("[ERROR] Staging buffer creation failed: {}", (int)staging_buffer_res.error());
            return std::unexpected(staging_buffer_res.error());
        }

        staging_buffer = std::move(staging_buffer_res.value());

        // Bake font into one or more bitmaps (if glyphs don't fit into a single image)
        int first_char = 32;
        int total_chars = 96;
        int remaining = total_chars;
        int atlas_index = 0;

        while (remaining > 0)
        {
            // fetch glyph info writes glyphs into glyph_map and stamps the current atlas_index
            auto fetch_glyph_info = [&out, &atlas_index](int codepoint, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, int advance)
            {
                out.m_glyph_map.emplace(codepoint, Glyph{
                    .x0 = static_cast<int16_t>(x0),
                    .y0 = static_cast<int16_t>(y0),
                    .x1 = static_cast<int16_t>(x1),
                    .y1 = static_cast<int16_t>(y1),
                    .u0 = static_cast<int16_t>(u0),
                    .v0 = static_cast<int16_t>(v0),
                    .u1 = static_cast<int16_t>(u1),
                    .v1 = static_cast<int16_t>(v1),
                    .advance = static_cast<int16_t>(advance),
                    .image_id = static_cast<int16_t>(atlas_index)
                });

                out.m_advance = advance;
            };

            int res = stbtt_BakeFontBitmap(
                font_data.data(),
                stbtt_GetFontOffsetForIndex(font_data.data(), 0),
                static_cast<float>(line_height),
                static_cast<unsigned char*>(staging_buffer.data()),
                static_cast<int>(bitmap_width),
                static_cast<int>(bitmap_height),
                first_char,
                remaining,
                fetch_glyph_info
            );

            if (res == 0)
            {
                std::println("[ERROR] Font baking failed.");
                return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
            }

            int baked_count = 0;
            if (res < 0)
            {
                baked_count = -res; // number of chars baked before running out
            }
            else
            {
                baked_count = remaining; // all requested chars fit
            }

            // Create GPU image for this atlas page
            VkImageCreateInfo image_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = VK_FORMAT_R8_UNORM, // do not forget change descriptor
                .extent = {bitmap_width, bitmap_height, 1},
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices = nullptr,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
            };

            VkImageViewCreateInfo view_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = VK_NULL_HANDLE,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = image_info.format,
                .components = engi::get_std_rgba_comp_mapping(),
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            auto image_result = Image::create(image_info, view_info);
            if (!image_result)
            {
                std::println("[ERROR] Font image creation failed: {}", (int)image_result.error());
                return std::unexpected(image_result.error());
            }

            out.m_atlases.push_back(std::move(image_result.value()));

            // Transition image to transfer-dst-optimal
            VkImageMemoryBarrier2 image_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = out.m_atlases.back().image(),
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VkDependencyInfo dep_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &image_barrier
            };

            vkCmdPipelineBarrier2(cmd, &dep_info);

            // Copy buffer to image
            VkBufferImageCopy copy_info = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = image_info.extent
            };

            vkCmdCopyBufferToImage(cmd, staging_buffer.buffer(), out.m_atlases.back().image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);

            // Transition image to shader-read-only-optimal
            image_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = out.m_atlases.back().image(),
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            dep_info = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &image_barrier
            };

            vkCmdPipelineBarrier2(cmd, &dep_info);

            // advance to next chunk
            first_char += baked_count;
            remaining -= baked_count;
            ++atlas_index;
        }

        // Ensure H and x glyphs exist
        auto glyph_h = out.m_glyph_map.find('H');
        auto glyph_x = out.m_glyph_map.find('x');
        if (glyph_h == out.m_glyph_map.end() || glyph_x == out.m_glyph_map.end())
        {
            std::println("[ERROR] Font glyphs not found: H or x");
            return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
        }

        out.m_cap_height = - glyph_h->second.y0;
        out.m_x_height = - glyph_x->second.y0;

        return out;
    }

    auto FontMonoAtlas::get_glyph(int codepoint) const noexcept -> const Glyph*
    {
        auto it = m_glyph_map.find(codepoint);
        if (it == m_glyph_map.end())
            return nullptr;
        return &it->second;
    }

    auto FontMonoAtlas::calculate_line_width(std::wstring_view text) const -> uint32_t
    {
        return text.size() * m_advance;
    }
    auto FontMonoAtlas::calculate_multiline_size(std::wstring_view text, uint32_t line_height) const -> go::vu2
    {
        uint32_t max_width = 0;
        uint32_t current_line_length = 0;
        uint32_t height = line_height;

        for (const wchar_t& ch : text)
        {
            if (ch == L'\n')
            {
                uint32_t line_width = current_line_length * m_advance;
                if (line_width > max_width)
                    max_width = line_width;

                current_line_length = 0;
                height += line_height;
                continue;
            }

            ++current_line_length;
        }

        uint32_t line_width = current_line_length * m_advance;
        if (line_width > max_width)
            max_width = line_width;

        return {max_width, height};
    }
}
