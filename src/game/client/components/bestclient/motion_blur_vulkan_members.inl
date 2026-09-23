/* Copyright © 2026 BestProject Team */
struct SFrameBlendImage
{
	VkImage m_Image = VK_NULL_HANDLE;
	SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> m_ImgMem;
	VkImageView m_ImgView = VK_NULL_HANDLE;
	SDeviceDescriptorSet m_DescriptorSet;
	bool m_Valid = false;
};
