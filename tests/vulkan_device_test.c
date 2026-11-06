/* Authored native Vulkan capability/transfer check. No API-version override. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "native_gpu_diagnostics.h"
#define FUNCTIONS(X) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkCreateCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkCmdFillBuffer) X(vkEndCommandBuffer) X(vkCreateFence) X(vkQueueSubmit) X(vkWaitForFences) X(vkDeviceWaitIdle) X(vkDestroyFence) X(vkDestroyCommandPool) X(vkUnmapMemory) X(vkDestroyBuffer) X(vkFreeMemory) X(vkDestroyDevice) X(vkDestroyInstance)
#define DECLARE(n) PFN_##n n;
FUNCTIONS(DECLARE)
#undef DECLARE
#define OK(x) do{VkResult r=(x);if(r!=VK_SUCCESS){printf("BEGONIA VULKAN: %s failed %d\n",#x,r);return 1;}}while(0)
static VKAPI_ATTR VkBool32 VKAPI_CALL diagnostic(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
 VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *message,void *context) {
 (void)type;(void)context;
 fprintf(stderr,"BEGONIA VULKAN: driver severity%x %s\n",severity,message->pMessage?message->pMessage:"no message");return VK_FALSE;
}
int main(void) {
 setvbuf(stdout,NULL,_IOLBF,0);native_gpu_install_crash_diagnostics();alarm(60);setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER","1",1);
 puts("BEGONIA VULKAN: ICD dlopen begin");
 void *icd=dlopen("/redwood/lib/libvulkan_panfrost.dylib",RTLD_NOW|RTLD_LOCAL);
 if(!icd){printf("BEGONIA VULKAN: dlopen error %s\n",dlerror());return 1;}
 puts("BEGONIA VULKAN: ICD dlopen complete");
 PFN_vkGetInstanceProcAddr get=(PFN_vkGetInstanceProcAddr)dlsym(icd,"vk_icdGetInstanceProcAddr");
 native_gpu_record_icd((void *)get);
 if(!get)return 1;
 PFN_vkCreateInstance create=(PFN_vkCreateInstance)get(NULL,"vkCreateInstance");if(!create)return 1;
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="begonia-native-check",.apiVersion=VK_API_VERSION_1_0};
 const char *debug_extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
 VkDebugUtilsMessengerCreateInfoEXT debug={.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
 .messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
 .messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,.pfnUserCallback=diagnostic};
 VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pNext=&debug,.pApplicationInfo=&app,.enabledExtensionCount=1,.ppEnabledExtensionNames=&debug_extension};VkInstance instance;
 puts("BEGONIA VULKAN: instance begin");OK(create(&info,NULL,&instance));
#define LOAD(n) n=(PFN_##n)get(instance,#n);if(!n){printf("BEGONIA VULKAN: missing %s\n",#n);return 1;}
 FUNCTIONS(LOAD)
#undef LOAD
 puts("BEGONIA VULKAN: physical enumeration begin");uint32_t count=1;VkPhysicalDevice physical;OK(vkEnumeratePhysicalDevices(instance,&count,&physical));if(!count)return 1;
 VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
 printf("BEGONIA VULKAN: device=%s API=%u.%u.%u vendor=%x id=%x\n",properties.deviceName,VK_VERSION_MAJOR(properties.apiVersion),VK_VERSION_MINOR(properties.apiVersion),VK_VERSION_PATCH(properties.apiVersion),properties.vendorID,properties.deviceID);
 count=0;OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));VkExtensionProperties *extensions=calloc(count,sizeof *extensions);if(!extensions)return 1;
 OK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions));
 const char *wanted[]={"VK_KHR_timeline_semaphore","VK_KHR_create_renderpass2","VK_KHR_buffer_device_address","VK_EXT_descriptor_indexing","VK_KHR_external_memory_fd","VK_EXT_external_memory_host","VK_KHR_synchronization2"};
 for(unsigned j=0;j<sizeof wanted/sizeof wanted[0];j++){int found=0;for(unsigned i=0;i<count;i++)if(!strcmp(extensions[i].extensionName,wanted[j]))found=1;printf("BEGONIA VULKAN: %s=%d\n",wanted[j],found);}free(extensions);
 count=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);VkQueueFamilyProperties *families=calloc(count,sizeof *families);if(!families)return 1;
 vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);uint32_t family=UINT32_MAX;
 for(unsigned i=0;i<count;i++)if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT){family=i;break;}free(families);if(family==UINT32_MAX)return 1;
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;
 puts("BEGONIA VULKAN: logical device begin");OK(vkCreateDevice(physical,&di,NULL,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
 VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=16384,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};VkBuffer buffer;OK(vkCreateBuffer(device,&bi,NULL,&buffer));
 VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);uint32_t type=UINT32_MAX;
 for(unsigned i=0;i<memory.memoryTypeCount;i++)if((requirements.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}if(type==UINT32_MAX)return 1;
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};VkDeviceMemory allocation;OK(vkAllocateMemory(device,&ai,NULL,&allocation));OK(vkBindBufferMemory(device,buffer,allocation,0));void *mapped;OK(vkMapMemory(device,allocation,0,16384,0,&mapped));memset(mapped,0,16384);
 VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};VkCommandPool pool;OK(vkCreateCommandPool(device,&pi,NULL,&pool));VkCommandBufferAllocateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;OK(vkAllocateCommandBuffers(device,&ci,&command));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};OK(vkBeginCommandBuffer(command,&begin));vkCmdFillBuffer(command,buffer,0,16384,0xbec07610);OK(vkEndCommandBuffer(command));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
 puts("BEGONIA VULKAN: transfer submit begin");OK(vkQueueSubmit(queue,1,&submit,fence));OK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));unsigned matches=0;for(unsigned i=0;i<4096;i++)if(((uint32_t *)mapped)[i]==0xbec07610)matches++;
 printf("BEGONIA VULKAN: actual GPU fill matches=%u/4096\n",matches);OK(vkDeviceWaitIdle(device));vkDestroyFence(device,fence,NULL);vkDestroyCommandPool(device,pool,NULL);vkUnmapMemory(device,allocation);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,allocation,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);alarm(0);return matches==4096?0:1;
}
