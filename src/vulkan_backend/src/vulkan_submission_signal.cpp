#include "vulkan_submission_signal.h"

namespace atlantis::vulkan_backend::detail {

VulkanSubmissionSignal::VulkanSubmissionSignal(VkSemaphore renderFinishedSemaphore)
    : semaphore_(renderFinishedSemaphore) {}

}  // namespace atlantis::vulkan_backend::detail
