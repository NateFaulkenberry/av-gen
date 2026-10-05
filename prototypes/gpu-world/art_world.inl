// B. Endless Meadow (stub)
struct WorldSystem final : System {
    std::string wgsl() const override { return "fn groundPos(uv: vec2f) -> vec2f { return uv * 100.0; }\nfn groundHeight(p: vec2f) -> f32 { return 0.0; }\nfn groundShade(w: vec3f, n: vec3f) -> vec3f { return vec3f(0.02); }\n"; }
    std::vector<MeshCpu> meshes() const override { return {makeOrb(glm::vec3(1))}; }
    std::vector<Bucket> buckets() const override { return {}; }
    std::uint32_t capacity() const override { return 4; }
    Cam camera(double, int) const override { return {}; }
    Look look() const override { return {}; }
    void init(gpu::Context&, const wgpu::ShaderModule&, const wgpu::BindGroupLayout&, const wgpu::Buffer&, const wgpu::Buffer&) override {}
    void setParams(FrameU&, double) override {}
    void compute(wgpu::CommandEncoder&, const wgpu::BindGroup&, gpu::FrameTimeline&) override {}
};
