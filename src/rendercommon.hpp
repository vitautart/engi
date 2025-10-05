#pragma once

#include <vulkan/vulkan.h>
#include <vector>


#define VK_CHECK_RETURN(result, msg) if (result != VK_SUCCESS) { std::println(msg, (int)result); return false; } 
#define VK_CHECK_PRINT(result, msg) if (result != VK_SUCCESS) { std::println(msg, (int)result); } 
#define OPT_CHECK_RETURN(opt, msg) if (!opt.has_value()) { std::println(msg); return false; } 

namespace engi {
    
template<typename FUNC, typename OUT, typename ... IN>
auto vkenum(FUNC pFunc, std::vector<OUT>& pOut, IN... pIn) noexcept -> VkResult
    requires (std::is_invocable_r_v<VkResult, decltype(pFunc), IN..., uint32_t*, OUT*>)
{
    uint32_t count = 0;
    auto res = pFunc(pIn..., &count, nullptr);
    if (res != VK_SUCCESS) 
        return res;
    
    pOut = std::vector<OUT>(count);
    return pFunc(pIn..., &count, pOut.data());
}

template<typename FUNC, typename OUT,  typename ...IN>
auto vkenum(FUNC pFunc, std::vector<OUT>& pOut, IN... pIn) noexcept -> void
{
    uint32_t count = 0;
    pFunc(pIn..., &count, nullptr);
    pOut = std::vector<OUT>(count);
    return pFunc(pIn..., &count, pOut.data());
}

}