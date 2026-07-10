//
// test_ResourceCreateValidation.cpp
//
#include "TestCommon.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12ResourceCreate.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12ResourceValidation.hpp>

#include <cstdint>
#include <string>

using namespace D3D12CoreLib;

namespace {

bool HasIssue(
    const D3D12ValidationResult& result,
    D3D12ValidationIssueCode code) {
    for (const auto& issue : result.issues) {
        if (issue.code == code) return true;
    }
    return false;
}

} // namespace

TEST(ResourceCreateValidation, CreatesDetailedDefaultBuffer) {
    REQUIRE_CORE(core);

    D3D12BufferCreateDesc desc;
    desc.sizeBytes = 4096;
    desc.alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.heapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    desc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.initialState = D3D12_RESOURCE_STATE_COMMON;

    D3D12Resource buffer = CreateBufferDetailed(*core, desc);
    CHECK(buffer.Get() != nullptr);
    CHECK(buffer.GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    CHECK_EQ(buffer.GetDesc().Width, 4096ull);
    CHECK_EQ(buffer.GetDesc().Alignment,
             static_cast<UINT64>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT));
    CHECK((buffer.GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);
    CHECK(buffer.GetState() == D3D12_RESOURCE_STATE_COMMON);

    D3D12_HEAP_PROPERTIES heapProperties = {};
    D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE;
    CHECK(SUCCEEDED(buffer.Get()->GetHeapProperties(&heapProperties, &heapFlags)));
    CHECK(heapProperties.Type == D3D12_HEAP_TYPE_DEFAULT);
    CHECK_EQ(heapProperties.CreationNodeMask, 1u);
    CHECK_EQ(heapProperties.VisibleNodeMask, 1u);
    CHECK(heapFlags == D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS);
}

TEST(ResourceCreateValidation, CreatesUploadAndReadbackBuffers) {
    REQUIRE_CORE(core);

    D3D12BufferCreateDesc uploadDesc;
    uploadDesc.sizeBytes = 1024;
    uploadDesc.heapType = D3D12_HEAP_TYPE_UPLOAD;
    uploadDesc.initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    D3D12Resource upload = CreateBufferDetailed(*core, uploadDesc);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    D3D12_HEAP_FLAGS uploadFlags = D3D12_HEAP_FLAG_NONE;
    CHECK(SUCCEEDED(upload.Get()->GetHeapProperties(&uploadHeap, &uploadFlags)));
    CHECK(uploadHeap.Type == D3D12_HEAP_TYPE_UPLOAD);

    D3D12BufferCreateDesc readbackDesc;
    readbackDesc.sizeBytes = 1024;
    readbackDesc.heapType = D3D12_HEAP_TYPE_READBACK;
    readbackDesc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    D3D12Resource readback = CreateBufferDetailed(*core, readbackDesc);

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    D3D12_HEAP_FLAGS readbackFlags = D3D12_HEAP_FLAG_NONE;
    CHECK(SUCCEEDED(readback.Get()->GetHeapProperties(&readbackHeap, &readbackFlags)));
    CHECK(readbackHeap.Type == D3D12_HEAP_TYPE_READBACK);

    uploadDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
    CHECK_THROWS(CreateBufferDetailed(*core, uploadDesc));
    readbackDesc.initialState = D3D12_RESOURCE_STATE_COMMON;
    CHECK_THROWS(CreateBufferDetailed(*core, readbackDesc));

    D3D12BufferCreateDesc invalidNodeMask;
    invalidNodeMask.sizeBytes = 256;
    invalidNodeMask.creationNodeMask = 1;
    invalidNodeMask.visibleNodeMask = 2;
    CHECK_THROWS(CreateBufferDetailed(*core, invalidNodeMask));
}

TEST(ResourceCreateValidation, CreatesDetailedTexture2D) {
    REQUIRE_CORE(core);

    D3D12Texture2DCreateDesc desc;
    desc.width = 64;
    desc.height = 32;
    desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.arraySize = 2;
    desc.mipLevels = 2;
    desc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.initialState = D3D12_RESOURCE_STATE_COMMON;

    D3D12Resource texture = CreateTexture2DDetailed(*core, desc);
    const D3D12_RESOURCE_DESC actual = texture.GetDesc();
    CHECK(actual.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
    CHECK_EQ(actual.Width, 64ull);
    CHECK_EQ(actual.Height, 32u);
    CHECK_EQ(actual.DepthOrArraySize, 2u);
    CHECK_EQ(actual.MipLevels, 2u);
    CHECK(actual.Format == DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK((actual.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0);

    desc.format = DXGI_FORMAT_UNKNOWN;
    CHECK_THROWS(CreateTexture2DDetailed(*core, desc));
}

TEST(ResourceCreateValidation, AcceptsMatchingTextureRequirement) {
    REQUIRE_CORE(core);

    D3D12Texture2DCreateDesc desc;
    desc.width = 128;
    desc.height = 64;
    desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12Resource texture = CreateTexture2DDetailed(*core, desc);

    D3D12Texture2DRequirement requirement;
    requirement.width = 128;
    requirement.height = 64;
    requirement.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    requirement.arraySize = 1;
    requirement.mipLevels = 1;
    requirement.sampleCount = 1;
    requirement.requiredFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    requirement.expectedDevice = core->GetDevice();
    requirement.widthMultiple = 2;
    requirement.heightMultiple = 2;

    const D3D12ValidationResult result = ValidateTexture2D(texture.Get(), requirement);
    CHECK(result.IsValid());
    CHECK(result.Message().empty());
    CHECK_NOTHROW(ValidateTexture2DOrThrow(texture.Get(), requirement));
}

TEST(ResourceCreateValidation, ReportsMultipleTextureMismatches) {
    REQUIRE_CORE(core);

    D3D12Texture2DCreateDesc desc;
    desc.width = 64;
    desc.height = 32;
    desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12Resource texture = CreateTexture2DDetailed(*core, desc);

    D3D12Texture2DRequirement requirement;
    requirement.width = 63;
    requirement.height = 31;
    requirement.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    requirement.arraySize = 2;
    requirement.mipLevels = 2;
    requirement.sampleCount = 4;
    requirement.requiredFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    requirement.forbiddenFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    requirement.expectedDevice =
        reinterpret_cast<ID3D12Device*>(static_cast<uintptr_t>(1));
    requirement.widthMultiple = 7;
    requirement.heightMultiple = 3;

    const D3D12ValidationResult result = ValidateTexture2D(texture.Get(), requirement);
    CHECK(!result.IsValid());
    CHECK(HasIssue(result, D3D12ValidationIssueCode::WidthMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::HeightMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::FormatMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::ArraySizeMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::MipLevelsMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::SampleCountMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::MissingResourceFlags));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::ForbiddenResourceFlags));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::WidthMultipleMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::HeightMultipleMismatch));
    CHECK(HasIssue(result, D3D12ValidationIssueCode::DeviceMismatch));
    CHECK(result.Message().find("DXGI_FORMAT_B8G8R8A8_UNORM") != std::string::npos);
    CHECK(result.Message().find("DXGI_FORMAT_R8G8B8A8_UNORM") != std::string::npos);
    CHECK_THROWS(ValidateTexture2DOrThrow(texture.Get(), requirement));
}

TEST(ResourceCreateValidation, RejectsNullWrongDimensionAndInvalidRequirements) {
    REQUIRE_CORE(core);

    D3D12Texture2DRequirement requirement;
    const D3D12ValidationResult nullResult = ValidateTexture2D(nullptr, requirement);
    CHECK(HasIssue(nullResult, D3D12ValidationIssueCode::NullResource));

    D3D12BufferCreateDesc bufferDesc;
    bufferDesc.sizeBytes = 256;
    D3D12Resource buffer = CreateBufferDetailed(*core, bufferDesc);
    const D3D12ValidationResult bufferResult = ValidateTexture2D(buffer.Get(), requirement);
    CHECK(HasIssue(bufferResult, D3D12ValidationIssueCode::WrongDimension));

    requirement.widthMultiple = 0;
    requirement.heightMultiple = 0;
    requirement.requiredFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    requirement.forbiddenFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const D3D12ValidationResult invalidRequirement =
        ValidateTexture2D(buffer.Get(), requirement);
    CHECK(HasIssue(invalidRequirement, D3D12ValidationIssueCode::InvalidRequirement));
}
