/* Copyright © 2026 BestProject Team */
void ResetFrameBlendHistory()
{
	for(auto &FrameBlendImage : m_vFrameBlendImages)
		FrameBlendImage.m_Valid = false;
	m_FrameBlendLastTime = 0;
}

[[nodiscard]] bool CreateFrameBlendDescriptorSet(SFrameBlendImage &FrameBlendImage)
{
	VkDescriptorPool DescriptorPool;
	if(!GetDescriptorPoolForAlloc(DescriptorPool, m_StandardTextureDescrPool, &FrameBlendImage.m_DescriptorSet, 1))
		return false;

	VkDescriptorSetAllocateInfo DesAllocInfo{};
	DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	DesAllocInfo.descriptorPool = DescriptorPool;
	DesAllocInfo.descriptorSetCount = 1;
	DesAllocInfo.pSetLayouts = &m_StandardTexturedDescriptorSetLayout;
	if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &FrameBlendImage.m_DescriptorSet.m_Descriptor) != VK_SUCCESS)
		return false;

	VkDescriptorImageInfo ImageInfo{};
	ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ImageInfo.imageView = FrameBlendImage.m_ImgView;
	ImageInfo.sampler = GetTextureSampler(SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE);

	std::array<VkWriteDescriptorSet, 1> aDescriptorWrites{};
	aDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	aDescriptorWrites[0].dstSet = FrameBlendImage.m_DescriptorSet.m_Descriptor;
	aDescriptorWrites[0].dstBinding = 0;
	aDescriptorWrites[0].dstArrayElement = 0;
	aDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	aDescriptorWrites[0].descriptorCount = 1;
	aDescriptorWrites[0].pImageInfo = &ImageInfo;

	vkUpdateDescriptorSets(m_VKDevice, static_cast<uint32_t>(aDescriptorWrites.size()), aDescriptorWrites.data(), 0, nullptr);
	return true;
}

[[nodiscard]] bool CreateFrameBlendImages()
{
	m_vFrameBlendImages.resize(m_SwapChainImageCount);

	for(auto &FrameBlendImage : m_vFrameBlendImages)
	{
		if(!CreateImage(m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width, m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height, 1, 1, m_VKSurfFormat.format, VK_IMAGE_TILING_OPTIMAL, FrameBlendImage.m_Image, FrameBlendImage.m_ImgMem, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
			return false;
		FrameBlendImage.m_ImgView = CreateImageView(FrameBlendImage.m_Image, m_VKSurfFormat.format, VK_IMAGE_VIEW_TYPE_2D, 1, 1);
		if(!ImageBarrier(FrameBlendImage.m_Image, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			return false;
		if(!ImageBarrier(FrameBlendImage.m_Image, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;
		if(!CreateFrameBlendDescriptorSet(FrameBlendImage))
			return false;
	}

	ResetFrameBlendHistory();
	m_FrameBlendEnabledLastFrame = false;
	return true;
}

void DestroyFrameBlendImages()
{
	for(auto &FrameBlendImage : m_vFrameBlendImages)
	{
		if(FrameBlendImage.m_DescriptorSet.m_Descriptor != VK_NULL_HANDLE)
			FreeDescriptorSetFromPool(FrameBlendImage.m_DescriptorSet);
		if(FrameBlendImage.m_ImgView != VK_NULL_HANDLE)
			vkDestroyImageView(m_VKDevice, FrameBlendImage.m_ImgView, nullptr);
		if(FrameBlendImage.m_Image != VK_NULL_HANDLE)
			vkDestroyImage(m_VKDevice, FrameBlendImage.m_Image, nullptr);
		if(FrameBlendImage.m_ImgMem.m_BufferMem.m_Mem != VK_NULL_HANDLE)
			FreeImageMemBlock(FrameBlendImage.m_ImgMem);
	}

	m_vFrameBlendImages.clear();
	m_FrameBlendEnabledLastFrame = false;
	m_FrameBlendLastTime = 0;
}

[[nodiscard]] bool FlushManualVertexBufferRange(const SDeviceMemoryBlock &BufferMem, size_t Offset, size_t DataSize)
{
	VkMappedMemoryRange MemRange{};
	MemRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	MemRange.memory = BufferMem.m_Mem;
	MemRange.offset = Offset;
	auto AlignmentMod = ((VkDeviceSize)DataSize % m_NonCoherentMemAlignment);
	auto AlignmentReq = (m_NonCoherentMemAlignment - AlignmentMod);
	if(AlignmentMod == 0)
		AlignmentReq = 0;
	MemRange.size = DataSize + AlignmentReq;
	if(MemRange.offset + MemRange.size > BufferMem.m_Size)
		MemRange.size = VK_WHOLE_SIZE;
	return vkFlushMappedMemoryRanges(m_VKDevice, 1, &MemRange) == VK_SUCCESS;
}

void FrameBlendImageBarrier(VkCommandBuffer &CommandBuffer, VkImage Image, VkImageLayout OldLayout, VkImageLayout NewLayout)
{
	VkImageMemoryBarrier Barrier{};
	Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	Barrier.oldLayout = OldLayout;
	Barrier.newLayout = NewLayout;
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.image = Image;
	Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Barrier.subresourceRange.baseMipLevel = 0;
	Barrier.subresourceRange.levelCount = 1;
	Barrier.subresourceRange.baseArrayLayer = 0;
	Barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	VkPipelineStageFlags DestinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

	if(OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		SourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		DestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if(OldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
		Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		SourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
	{
		Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		DestinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
	else if(OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		DestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else
	{
		dbg_assert_failed("Unsupported frame blend layout transition. OldLayout=%d NewLayout=%d", (int)OldLayout, (int)NewLayout);
	}

	vkCmdPipelineBarrier(CommandBuffer, SourceStage, DestinationStage, 0, 0, nullptr, 0, nullptr, 1, &Barrier);
}

[[nodiscard]] bool RenderFrameBlend(VkCommandBuffer &MainCommandBuffer)
{
	if(m_vFrameBlendImages.empty() || m_LastPresentedSwapChainImageIndex == std::numeric_limits<decltype(m_LastPresentedSwapChainImageIndex)>::max())
		return true;

	auto &FrameBlendImage = m_vFrameBlendImages[m_LastPresentedSwapChainImageIndex];
	if(!FrameBlendImage.m_Valid)
		return true;

	const float TotalBlendStrength = (g_Config.m_BcMotionBlurStrength / 95.0f) * 4.0f;
	if(TotalBlendStrength <= 0.0f)
		return true;
	constexpr float MaxBlendAlphaPerPass = 0.85f;
	const int RefPassCount = std::max(1, (int)std::ceil(TotalBlendStrength / MaxBlendAlphaPerPass));
	const float RefAlphaPerPass = TotalBlendStrength / (float)RefPassCount;
	const float RefAlpha60 = 1.0f - std::pow(1.0f - RefAlphaPerPass, RefPassCount);
	const float FrameTime = BcMotionBlurAdvanceFrameTime(m_FrameBlendLastTime);
	const float EffectiveAlpha = BcMotionBlurPersistence(RefAlpha60, FrameTime);
	if(EffectiveAlpha <= 0.0f)
		return true;

	const int BlendPassCount = std::max(1, (int)std::ceil(std::log(1.0f - EffectiveAlpha) / std::log(1.0f - MaxBlendAlphaPerPass)));
	const float BlendAlphaPerPass = 1.0f - std::pow(1.0f - EffectiveAlpha, 1.0f / (float)BlendPassCount);

	const bool UseSecondaryCommandBuffer = m_ThreadCount > 1;
	VkCommandBuffer CommandBuffer = MainCommandBuffer;
	if(UseSecondaryCommandBuffer)
	{
		CommandBuffer = m_vFrameBlendCommandBuffers[m_CurImageIndex];
		vkResetCommandBuffer(CommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

		VkCommandBufferInheritanceInfo InheritanceInfo{};
		InheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		InheritanceInfo.framebuffer = m_vFramebufferList[m_CurImageIndex];
		InheritanceInfo.occlusionQueryEnable = VK_FALSE;
		InheritanceInfo.renderPass = m_VKRenderPass;
		InheritanceInfo.subpass = 0;

		VkCommandBufferBeginInfo BeginInfo{};
		BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		BeginInfo.pInheritanceInfo = &InheritanceInfo;

		if(vkBeginCommandBuffer(CommandBuffer, &BeginInfo) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Frame blend command buffer cannot be filled anymore.");
			return false;
		}
	}

	CCommandBuffer::SState State{};
	State.m_BlendMode = EBlendMode::ALPHA;
	State.m_WrapMode = EWrapMode::CLAMP;
	State.m_Texture = 0;
	State.m_ScreenTL = vec2(0.0f, 0.0f);
	auto Viewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
	State.m_ScreenBR = vec2((float)Viewport.width, (float)Viewport.height);
	State.m_ClipEnable = false;

	SRenderCommandExecuteBuffer ExecBuffer{};
	ExecBuffer.m_ThreadIndex = MAIN_THREAD_INDEX;
	ExecBuffer.m_IndexBuffer = m_IndexBuffer;
	ExecBufferFillDynamicStates(State, ExecBuffer);

	bool IsTextured;
	size_t BlendModeIndex;
	size_t DynamicIndex;
	size_t AddressModeIndex;
	GetStateIndices(ExecBuffer, State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);

	auto &PipeLayout = GetStandardPipeLayout(false, true, BlendModeIndex, DynamicIndex);
	auto &PipeLine = GetStandardPipe(false, true, BlendModeIndex, DynamicIndex);
	vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLine);
	if(ExecBuffer.m_HasDynamicState)
	{
		vkCmdSetViewport(CommandBuffer, 0, 1, &ExecBuffer.m_Viewport);
		vkCmdSetScissor(CommandBuffer, 0, 1, &ExecBuffer.m_Scissor);
	}

	std::array<CCommandBuffer::SVertex, 4> aVertices{};
	aVertices[0].m_Pos = vec2(0.0f, 0.0f);
	aVertices[1].m_Pos = vec2((float)Viewport.width, 0.0f);
	aVertices[2].m_Pos = vec2((float)Viewport.width, (float)Viewport.height);
	aVertices[3].m_Pos = vec2(0.0f, (float)Viewport.height);
	aVertices[0].m_Tex = vec2(0.0f, 0.0f);
	aVertices[1].m_Tex = vec2(1.0f, 0.0f);
	aVertices[2].m_Tex = vec2(1.0f, 1.0f);
	aVertices[3].m_Tex = vec2(0.0f, 1.0f);
	for(auto &Vertex : aVertices)
	{
		Vertex.m_Color.r = 255;
		Vertex.m_Color.g = 255;
		Vertex.m_Color.b = 255;
		Vertex.m_Color.a = (uint8_t)std::round(BlendAlphaPerPass * 255.0f);
	}

	VkBuffer VKBuffer;
	SDeviceMemoryBlock VKBufferMem;
	size_t BufferOff = 0;
	if(!CreateStreamVertexBuffer(MAIN_THREAD_INDEX, VKBuffer, VKBufferMem, BufferOff, aVertices.data(), sizeof(CCommandBuffer::SVertex) * aVertices.size()))
		return false;
	if(!FlushManualVertexBufferRange(VKBufferMem, BufferOff, sizeof(CCommandBuffer::SVertex) * aVertices.size()))
		return false;

	std::array<VkBuffer, 1> aVertexBuffers = {VKBuffer};
	std::array<VkDeviceSize, 1> aOffsets = {(VkDeviceSize)BufferOff};
	vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
	vkCmdBindIndexBuffer(CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

	std::array<float, 4 * 2> aMatrix;
	GetStateMatrix(State, aMatrix);
	vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), aMatrix.data());
	vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, 0, 1, &FrameBlendImage.m_DescriptorSet.m_Descriptor, 0, nullptr);
	for(int Pass = 0; Pass < BlendPassCount; ++Pass)
		vkCmdDrawIndexed(CommandBuffer, 6, 1, 0, 0, 0);

	if(UseSecondaryCommandBuffer)
	{
		if(vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Frame blend command buffer cannot be ended anymore.");
			return false;
		}
		vkCmdExecuteCommands(MainCommandBuffer, 1, &CommandBuffer);
	}

	return true;
}

void CopyFrameToFrameBlendHistory(VkCommandBuffer &CommandBuffer)
{
	if(m_vFrameBlendImages.empty())
		return;

	auto &FrameBlendImage = m_vFrameBlendImages[m_CurImageIndex];
	auto &SwapImage = m_vSwapChainImages[m_CurImageIndex];

	FrameBlendImageBarrier(CommandBuffer, FrameBlendImage.m_Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	FrameBlendImageBarrier(CommandBuffer, SwapImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	VkImageCopy Region{};
	Region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.srcSubresource.mipLevel = 0;
	Region.srcSubresource.baseArrayLayer = 0;
	Region.srcSubresource.layerCount = 1;
	Region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	Region.dstSubresource.mipLevel = 0;
	Region.dstSubresource.baseArrayLayer = 0;
	Region.dstSubresource.layerCount = 1;
	Region.extent.width = m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width;
	Region.extent.height = m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height;
	Region.extent.depth = 1;

	vkCmdCopyImage(CommandBuffer, SwapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, FrameBlendImage.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

	FrameBlendImageBarrier(CommandBuffer, FrameBlendImage.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	FrameBlendImageBarrier(CommandBuffer, SwapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	FrameBlendImage.m_Valid = true;
}
