// ymo_loader.cpp — Falcom YMO binary parser & GPU mesh builder implementation
#include "ymo_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <rlgl.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace falcom {

ParsedYmoModel::ParsedYmoModel()
    : version(9), total_vertices(0), total_triangles(0), is_loaded(false) {
    memset(&raylib_model, 0, sizeof(Model));
    bounds = { { 0, 0, 0 }, { 0, 0, 0 } };
    center = { 0, 0, 0 };
    radius = 1.0f;
}

ParsedYmoModel::~ParsedYmoModel() {
    if (is_loaded) {
        if (raylib_model.meshes) {
            for (int i = 0; i < raylib_model.meshCount; i++) {
                if (IsWindowReady()) {
                    UnloadMesh(raylib_model.meshes[i]);
                } else {
                    if (raylib_model.meshes[i].vertices) RL_FREE(raylib_model.meshes[i].vertices);
                    if (raylib_model.meshes[i].normals) RL_FREE(raylib_model.meshes[i].normals);
                    if (raylib_model.meshes[i].texcoords) RL_FREE(raylib_model.meshes[i].texcoords);
                    if (raylib_model.meshes[i].colors) RL_FREE(raylib_model.meshes[i].colors);
                }
            }
            RL_FREE(raylib_model.meshes);
            raylib_model.meshes = nullptr;
        }
        if (raylib_model.meshMaterial) {
            RL_FREE(raylib_model.meshMaterial);
            raylib_model.meshMaterial = nullptr;
        }
        if (raylib_model.materials) {
            RL_FREE(raylib_model.materials);
            raylib_model.materials = nullptr;
        }
        is_loaded = false;
    }
}

std::shared_ptr<ParsedYmoModel> YmoLoader::load_from_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 68) {
        fclose(f);
        return nullptr;
    }
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) {
        fclose(f);
        return nullptr;
    }
    fclose(f);

    size_t last_slash = path.find_last_of("/\\");
    std::string fname = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;
    return load_from_memory(buf.data(), buf.size(), fname);
}

std::shared_ptr<ParsedYmoModel> YmoLoader::load_from_memory(const uint8_t* data, size_t size, const std::string& filename) {
    if (!data || size < 68) return nullptr;

    if (memcmp(data, "YMO\0", 4) != 0) {
        return nullptr;
    }

    auto model = std::make_shared<ParsedYmoModel>();
    model->filename = filename;

    const uint32_t* hdr = (const uint32_t*)data;
    model->version = hdr[1];
    uint32_t f1 = hdr[2];
    bool is_v17 = (model->version == 17 || f1 == 17 || model->version == 0x0004FD59);
    uint32_t mat_count = is_v17 ? hdr[4] : hdr[5];
    uint32_t motion_count = is_v17 ? hdr[5] : hdr[6];
    uint32_t mesh_count = is_v17 ? hdr[6] : hdr[7];
    uint32_t node_count = is_v17 ? hdr[7] : hdr[8];
    (void)mesh_count;

    // Read collision file references
    size_t coll_start = is_v17 ? 0x004C : 0x0078;
    size_t pos = coll_start;
    for (int i = 0; i < 3; i++) {
        if (pos + 80 <= size) {
            char coll_name[81] = {};
            memcpy(coll_name, data + pos, 80);
            coll_name[80] = '\0';
            std::string s = coll_name;
            size_t null_idx = s.find('\0');
            if (null_idx != std::string::npos) s = s.substr(0, null_idx);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
            if (!s.empty()) model->collision_files.push_back(s);
            pos = is_v17 ? (pos + 0xC0) : (pos + 80);
        }
    }

    // Materials start offset
    size_t mat_start = is_v17 ? 0x02AC : ((motion_count > 0) ? (0x0170 + motion_count * 68) : 0x0178);
    const size_t MAT_RECORD_SIZE = is_v17 ? 748 : 388;

    model->materials.resize(mat_count);
    for (uint32_t i = 0; i < mat_count; i++) {
        size_t m_pos = mat_start + i * MAT_RECORD_SIZE;
        if (m_pos + MAT_RECORD_SIZE > size) break;

        const uint8_t* m_ptr = data + m_pos;
        uint32_t flags = *(const uint32_t*)(m_ptr + 0);
        float alpha = *(const float*)(m_ptr + 4);

        char tex_str[249] = {};
        memcpy(tex_str, m_ptr + 0x8C, 248);
        tex_str[248] = '\0';
        std::string tex_path = tex_str;
        size_t null_pos = tex_path.find('\0');
        if (null_pos != std::string::npos) tex_path = tex_path.substr(0, null_pos);
        while (!tex_path.empty() && (tex_path.back() == ' ' || tex_path.back() == '\r' || tex_path.back() == '\n')) tex_path.pop_back();

        std::string tex_name;
        size_t last_s = tex_path.find_last_of("/\\");
        if (last_s != std::string::npos) tex_name = tex_path.substr(last_s + 1);
        else tex_name = tex_path;

        YmoMaterialInfo& mat = model->materials[i];
        mat.index = (int)i;
        mat.flags = flags;
        mat.alpha = (alpha > 0.001f) ? alpha : 1.0f;
        mat.texture_path = tex_path;
        mat.texture_name = tex_name;
        mat.texture = { 0, 0, 0, 0, 0 };
        mat.has_custom_texture = false;
    }

    // Nodes
    size_t node_start = mat_start + mat_count * MAT_RECORD_SIZE;
    if (motion_count > 0 && !is_v17) node_start -= 8;
    size_t node_rec_size = is_v17 ? 576 : 240;

    model->nodes.resize(node_count);
    pos = node_start;
    for (uint32_t i = 0; i < node_count; i++) {
        if (pos + node_rec_size > size) break;

        char n_name[17] = {}, p_name[17] = {};
        memcpy(n_name, data + pos, 16);
        memcpy(p_name, data + pos + 16, 16);

        const float* m_vals = (const float*)(data + pos + 32);
        Matrix m = {
            m_vals[0], m_vals[1], m_vals[2], m_vals[3],
            m_vals[4], m_vals[5], m_vals[6], m_vals[7],
            m_vals[8], m_vals[9], m_vals[10], m_vals[11],
            m_vals[12], m_vals[13], m_vals[14], m_vals[15]
        };

        model->nodes[i].name = n_name;
        model->nodes[i].parent_name = p_name;
        model->nodes[i].matrix = m;

        pos += node_rec_size;
    }

    // Meshes start after node table
    size_t first_mesh_start = is_v17 ? pos : (node_start + node_count * 240 + 80);
    std::vector<size_t> mesh_offsets;
    for (size_t p = first_mesh_start; p + 16 < size; p++) {
        if (data[p] == 'm' && data[p+1] == '_') {
            bool valid = true;
            for (int k = 2; k < 6; k++) {
                char c = (char)data[p+k];
                if (!isalnum((unsigned char)c) && c != '_') { valid = false; break; }
            }
            if (valid && data[p+6] == '\0') {
                mesh_offsets.push_back(p);
                p += 16;
            }
        }
    }
    if (mesh_offsets.empty()) {
        for (size_t p = first_mesh_start; p + 20 < size; p += 4) {
            uint32_t v_cnt = *(const uint32_t*)(data + p);
            uint32_t st = *(const uint32_t*)(data + p + 4);
            if ((st == 36 || st == 40 || st == 48) && v_cnt > 0 && v_cnt < 200000) {
                mesh_offsets.push_back(p);
                break;
            }
        }
    }

    if (mesh_offsets.empty() && first_mesh_start < size - 32) {
        mesh_offsets.push_back(first_mesh_start);
    }

    struct RawSubmesh {
        int mat_idx;
        uint32_t tri_count;
        uint32_t v_start;
        uint32_t v_count;
        std::vector<uint16_t> indices;
    };

    struct ParsedMeshData {
        std::string name;
        uint32_t total_verts;
        std::vector<Vector3> positions;
        std::vector<Vector3> normals;
        std::vector<Color> colors;
        std::vector<Vector2> uvs;
        std::vector<RawSubmesh> submeshes;
    };

    std::vector<ParsedMeshData> parsed_meshes;
    for (size_t m_offset : mesh_offsets) {
        pos = m_offset;
        if (pos + 16 > size) continue;

        char m_name[17] = {};
        if (!is_v17) {
            memcpy(m_name, data + pos, 16);
            pos += 16;
        } else {
            snprintf(m_name, sizeof(m_name), "mesh_%04d", (int)parsed_meshes.size());
        }

        ParsedMeshData pmesh;
        pmesh.name = m_name;

        // Submesh table (v9/v10 format)
        if (!is_v17) {
            while (pos + 32 <= size) {
                const uint32_t* sm_vals = (const uint32_t*)(data + pos);
                uint32_t f0 = sm_vals[0]; // tri_count
                uint32_t f1 = sm_vals[1]; // v_start
                uint32_t f2 = sm_vals[2]; // v_count
                uint32_t f6 = sm_vals[6]; // material index (1-based)

                if (f0 == 0) { pos += 32; break; }
                if ((f1 == 36 || f1 == 40 || f1 == 48) && f2 == 0) { pos += 32; break; }

                int res_mat = 0;
                if (mat_count > 0) {
                    res_mat = (f6 > 0) ? ((int)(f6 - 1) % (int)mat_count) : ((int)mat_count - 1);
                }

                RawSubmesh rsm;
                rsm.mat_idx = res_mat;
                rsm.tri_count = f0;
                rsm.v_start = f1;
                rsm.v_count = f2;
                pmesh.submeshes.push_back(rsm);

                pos += 32;
                if (pmesh.submeshes.size() >= mat_count && mat_count > 0) break;
            }
        }

        uint32_t total_v = 0;
        for (const auto& sm : pmesh.submeshes) {
            total_v = std::max(total_v, sm.v_start + sm.v_count);
        }
        pmesh.total_verts = total_v;

        size_t v_start = pos;
        uint32_t stride = is_v17 ? 48 : 40;
        uint32_t total_indices = 0;
        if (is_v17) {
            size_t desc_pos = 0;
            for (size_t p = 0x02AC; p + 48 < size; p += 4) {
                uint32_t v_cnt = *(const uint32_t*)(data + p);
                uint32_t st = *(const uint32_t*)(data + p + 4);
                uint32_t v_type = *(const uint32_t*)(data + p + 8);
                if ((st == 36 || st == 40 || st == 48) && v_cnt >= 10 && v_cnt <= 200000 && (v_type == 722 || v_type == 466 || v_type == 0x02D2)) {
                    size_t cand = p + 12;
                    if (cand + st <= size) {
                        const float* pf = (const float*)(data + cand);
                        const float* nf = (const float*)(data + cand + 12);
                        float n_sq = nf[0]*nf[0] + nf[1]*nf[1] + nf[2]*nf[2];
                        if (fabsf(n_sq - 1.0f) < 0.35f && fabsf(pf[0]) < 10000.0f && fabsf(pf[1]) < 10000.0f && fabsf(pf[2]) < 10000.0f) {
                            desc_pos = p;
                            total_v = v_cnt;
                            stride = st;
                            v_start = cand;
                            break;
                        }
                    }
                }
            }
            pmesh.total_verts = total_v;

            // Search for v17 submesh table before desc_pos
            if (desc_pos > 0 && mat_count > 0) {
                size_t sm_search_min = (desc_pos >= 32 * mat_count + 512) ? (desc_pos - 32 * mat_count - 512) : 0;
                for (size_t p = sm_search_min; p + 32 <= desc_pos; p += 4) {
                    uint32_t curr_tri = 0;
                    bool match = true;
                    std::vector<RawSubmesh> recs;
                    for (uint32_t si = 0; si < mat_count; si++) {
                        size_t r_pos = p + si * 32;
                        if (r_pos + 16 > size) { match = false; break; }
                        const uint32_t* u4 = (const uint32_t*)(data + r_pos);
                        uint32_t w1 = u4[1], w2 = u4[2], w3 = u4[3];
                        if (w1 == si && w2 == curr_tri && w3 < 200000) {
                            RawSubmesh rsm;
                            rsm.mat_idx = (int)si;
                            rsm.tri_count = w3;
                            rsm.v_start = 0;
                            rsm.v_count = total_v;
                            recs.push_back(rsm);
                            curr_tri += w3;
                        } else {
                            match = false;
                            break;
                        }
                    }
                    if (match && recs.size() == mat_count && curr_tri > 0) {
                        pmesh.submeshes = recs;
                        break;
                    }
                }
            }
        } else {
            size_t desc_pos = 0;
            for (size_t p = (pos >= 64 ? pos - 64 : 0); p + 12 <= size && p < pos + 256; p += 4) {
                uint32_t vc = *(const uint32_t*)(data + p);
                if (vc == total_v && total_v > 0) {
                    desc_pos = p;
                    break;
                }
            }
            if (desc_pos == 0) desc_pos = pos;

            uint32_t desc_stride = 0;
            uint32_t total_stream_stride = 0;
            if (desc_pos + 12 <= size) {
                uint32_t w1 = *(const uint32_t*)(data + desc_pos + 4);
                uint32_t w2 = *(const uint32_t*)(data + desc_pos + 8);
                if (w2 == 36 || w2 == 40 || w2 == 48) desc_stride = w2;
                else if (w1 == 36 || w1 == 40 || w1 == 48) desc_stride = w1;
                total_stream_stride = desc_stride;

                // Check for stream 1 descriptor at desc_pos + 32
                if (desc_pos + 32 + 12 <= size) {
                    uint32_t d1_v = *(const uint32_t*)(data + desc_pos + 32);
                    uint32_t d1_st = *(const uint32_t*)(data + desc_pos + 32 + 8);
                    if (d1_v == total_v && (d1_st == 4 || d1_st == 8 || d1_st == 12 || d1_st == 16)) {
                        total_stream_stride += d1_st;
                    }
                }
            }
            if (total_stream_stride == 0) total_stream_stride = (desc_stride > 0 ? desc_stride : 40);

            uint32_t expected_total_indices = 0;
            for (const auto& sm : pmesh.submeshes) {
                expected_total_indices += sm.tri_count * 3;
            }

            std::vector<std::pair<uint32_t, uint32_t>> cand_strides; // (unpack_stride, ib_stride)
            if (desc_stride > 0) {
                cand_strides.push_back({ desc_stride, total_stream_stride });
                for (uint32_t s : { 40u, 48u, 36u }) {
                    if (s != desc_stride) cand_strides.push_back({ s, s });
                }
            } else {
                cand_strides = { { 40u, 40u }, { 48u, 48u }, { 36u, 36u } };
            }

            size_t resolved_v_start = desc_pos + 28;
            uint32_t resolved_stride = desc_stride > 0 ? desc_stride : 40;
            size_t resolved_ib_pos = 0;
            uint32_t resolved_total_indices = 0;

            for (auto [cand_st, cand_ib_st] : cand_strides) {
                for (size_t cand_v : { desc_pos + 28, desc_pos + 60, desc_pos + 32, desc_pos + 24, desc_pos + 56, desc_pos + 64 }) {
                    if (cand_v + cand_st <= size) {
                        const float* f6 = (const float*)(data + cand_v);
                        float px = f6[0], py = f6[1], pz = f6[2];
                        float nx = f6[3], ny = f6[4], nz = f6[5];
                        if (!std::isnan(px) && !std::isnan(py) && !std::isnan(pz) &&
                            fabsf(px) < 10000.0f && fabsf(py) < 10000.0f && fabsf(pz) < 10000.0f) {
                            float n_sq = nx*nx + ny*ny + nz*nz;
                            if (fabsf(n_sq - 1.0f) < 0.35f || total_v < 100) {
                                size_t cand_v_end = cand_v + total_v * cand_ib_st;
                                for (size_t p_ib = cand_v_end; p_ib + 16 <= size && p_ib < cand_v_end + 64; p_ib += 4) {
                                    const uint32_t* ib_hdr = (const uint32_t*)(data + p_ib);
                                    uint32_t ti_b = ib_hdr[0];
                                    uint32_t b_type_b = ib_hdr[1];
                                    uint32_t p_type_b = ib_hdr[2];
                                    if (b_type_b == 101 && (p_type_b == 1 || p_type_b == 2) && ti_b > 0 && ti_b < 300000 && p_ib + 12 + (size_t)ti_b * 2 <= size) {
                                        if (expected_total_indices == 0 || ti_b == expected_total_indices) {
                                            resolved_v_start = cand_v;
                                            resolved_stride = cand_st;
                                            resolved_ib_pos = p_ib + 12;
                                            resolved_total_indices = ti_b;
                                            break;
                                        } else if (resolved_ib_pos == 0) {
                                            resolved_v_start = cand_v;
                                            resolved_stride = cand_st;
                                            resolved_ib_pos = p_ib + 12;
                                            resolved_total_indices = ti_b;
                                        }
                                    }
                                    if (p_ib + 32 <= size) {
                                        uint32_t ti_a = ib_hdr[1];
                                        uint32_t b_type_a = ib_hdr[2];
                                        uint32_t p_type_a = ib_hdr[3];
                                        if (b_type_a == 101 && (p_type_a == 1 || p_type_a == 2) && ti_a > 0 && ti_a < 300000 && p_ib + 32 + (size_t)ti_a * 2 <= size) {
                                            if (expected_total_indices == 0 || ti_a == expected_total_indices) {
                                                resolved_v_start = cand_v;
                                                resolved_stride = cand_st;
                                                resolved_ib_pos = p_ib + 32;
                                                resolved_total_indices = ti_a;
                                                break;
                                            } else if (resolved_ib_pos == 0) {
                                                resolved_v_start = cand_v;
                                                resolved_stride = cand_st;
                                                resolved_ib_pos = p_ib + 32;
                                                resolved_total_indices = ti_a;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (resolved_ib_pos > 0 && (expected_total_indices == 0 || resolved_total_indices == expected_total_indices)) break;
                }
                if (resolved_ib_pos > 0 && (expected_total_indices == 0 || resolved_total_indices == expected_total_indices)) break;
            }
            fprintf(stderr, "[DEBUG ymo] %s: desc_pos=0x%zx, v_start=0x%zx, stride=%u, ib_pos=0x%zx, total_indices=%u\n",
                filename.c_str(), desc_pos, resolved_v_start, resolved_stride, resolved_ib_pos, resolved_total_indices);
            v_start = resolved_v_start;
            stride = resolved_stride;
            total_indices = resolved_total_indices;
            pos = resolved_ib_pos > 0 ? resolved_ib_pos : (v_start + total_v * stride + 32);
        }
        pmesh.positions.resize(total_v);
        pmesh.normals.resize(total_v);
        pmesh.colors.resize(total_v);
        pmesh.uvs.resize(total_v);

        for (uint32_t vi = 0; vi < total_v; vi++) {
            size_t v_off = v_start + vi * stride;
            if (v_off + stride > size) break;

            float px = 0, py = 0, pz = 0;
            float nx = 0, ny = 0, nz = 1;
            uint32_t col = 0xFFFFFFFF;
            float u = 0, v = 0;

            if (stride == 36) {
                const float* fptr = (const float*)(data + v_off);
                px = fptr[0]; py = fptr[1]; pz = fptr[2];
                nx = fptr[3]; ny = fptr[4]; nz = fptr[5];
                col = *(const uint32_t*)(data + v_off + 24);
                u = *(const float*)(data + v_off + 28);
                v = *(const float*)(data + v_off + 32);
            } else if (stride == 48) {
                const float* fptr = (const float*)(data + v_off);
                px = fptr[0]; py = fptr[1]; pz = fptr[2];
                nx = fptr[3]; ny = fptr[4]; nz = fptr[5];
                col = *(const uint32_t*)(data + v_off + 24);
                u = *(const float*)(data + v_off + 32);
                v = *(const float*)(data + v_off + 36);
            } else { // stride 40
                const float* fptr = (const float*)(data + v_off);
                px = fptr[0]; py = fptr[1]; pz = fptr[2];
                nx = fptr[3]; ny = fptr[4]; nz = fptr[5];
                col = *(const uint32_t*)(data + v_off + 24);
                u = *(const float*)(data + v_off + 32);
                v = *(const float*)(data + v_off + 36);
            }

            if (std::isnan(px)) px = 0;
            if (std::isnan(py)) py = 0;
            if (std::isnan(pz)) pz = 0;
            if (std::isnan(nx)) nx = 0;
            if (std::isnan(ny)) ny = 0;
            if (std::isnan(nz)) nz = 1;
            if (std::isnan(u)) u = 0;
            if (std::isnan(v)) v = 0;

            pmesh.positions[vi] = { px, py, pz };
            pmesh.normals[vi] = { nx, ny, nz };
            uint8_t b = (uint8_t)(col & 0xFF);
            uint8_t g = (uint8_t)((col >> 8) & 0xFF);
            uint8_t r = (uint8_t)((col >> 16) & 0xFF);
            uint8_t a = 255;
            if (col == 0 || (r == 0 && g == 0 && b == 0)) {
                r = 255; g = 255; b = 255; a = 255;
            }
            pmesh.colors[vi] = { r, g, b, a };
            pmesh.uvs[vi] = { u, v };
        }

        // If v17 index search was used, extract indices
        if (is_v17) {
            size_t idx_search_pos = v_start + total_v * stride;
            for (size_t p = idx_search_pos; p + 16 <= size && p < idx_search_pos + 64; p += 4) {
                uint32_t ti = *(const uint32_t*)(data + p);
                if (ti > 0 && ti < 300000 && p + 12 + (size_t)ti * 2 <= size) {
                    const uint16_t* test_idx = (const uint16_t*)(data + p + 12);
                    if (test_idx[0] < total_v && test_idx[1] < total_v) {
                        total_indices = ti;
                        pos = p + 12;
                        break;
                    }
                }
            }
        }
        std::vector<uint16_t> all_indices;
        if (total_indices > 0 && pos + total_indices * 2 <= size) {
            const uint16_t* idx_ptr = (const uint16_t*)(data + pos);
            all_indices.assign(idx_ptr, idx_ptr + total_indices);
            pos += total_indices * 2;
        }

        // Slice submesh indices
        size_t curr_tri = 0;
        for (size_t smi = 0; smi < pmesh.submeshes.size(); smi++) {
            auto& sm = pmesh.submeshes[smi];
            size_t req_idx = (size_t)sm.tri_count * 3;
            if (curr_tri * 3 + req_idx <= all_indices.size()) {
                sm.indices.assign(all_indices.begin() + curr_tri * 3, all_indices.begin() + curr_tri * 3 + req_idx);
            }
            curr_tri += sm.tri_count;
        }

        if (pmesh.submeshes.empty() && !all_indices.empty()) {
            RawSubmesh sm;
            sm.mat_idx = 0;
            sm.tri_count = (uint32_t)(all_indices.size() / 3);
            sm.v_start = 0;
            sm.v_count = total_v;
            sm.indices = all_indices;
            pmesh.submeshes.push_back(sm);
        }
        parsed_meshes.push_back(pmesh);
    }

    // Now construct the Raylib Model!
    // Total submeshes across all meshes = Raylib Model.meshCount
    size_t total_submeshes = 0;
    for (const auto& pm : parsed_meshes) {
        total_submeshes += pm.submeshes.size();
    }

    if (total_submeshes == 0) {
        return nullptr;
    }

    model->raylib_model.meshCount = (int)total_submeshes;
    model->raylib_model.meshes = (Mesh*)RL_CALLOC((int)total_submeshes, sizeof(Mesh));
    model->raylib_model.meshMaterial = (int*)RL_CALLOC((int)total_submeshes, sizeof(int));

    int rl_mat_count = std::max(1, (int)model->materials.size());
    model->raylib_model.materialCount = rl_mat_count;
    model->raylib_model.materials = (Material*)RL_CALLOC(rl_mat_count, sizeof(Material));

    // Initialize Raylib materials
    for (int i = 0; i < rl_mat_count; i++) {
        if (IsWindowReady()) {
            model->raylib_model.materials[i] = LoadMaterialDefault();
            if (i < (int)model->materials.size()) {
                float a = (model->materials[i].alpha > 0.01f) ? model->materials[i].alpha : 1.0f;
                model->raylib_model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = ColorFromNormalized({ 1.0f, 1.0f, 1.0f, a });
            }
        }
    }

    Vector3 bmin = { 1e9f, 1e9f, 1e9f };
    Vector3 bmax = { -1e9f, -1e9f, -1e9f };

    int curr_mesh_idx = 0;
    for (const auto& pm : parsed_meshes) {
        for (const auto& sm : pm.submeshes) {
            if (sm.indices.empty() || sm.tri_count == 0 || sm.indices.size() < (size_t)sm.tri_count * 3 || pm.positions.empty()) {
                Mesh m = {};
                m.triangleCount = 0;
                m.vertexCount = 0;
                model->raylib_model.meshes[curr_mesh_idx] = m;
                model->raylib_model.meshMaterial[curr_mesh_idx] = sm.mat_idx;
                curr_mesh_idx++;
                continue;
            }

            int tri_count = (int)sm.tri_count;
            int v_count = tri_count * 3; // Unroll / direct indexed upload

            Mesh m = {};
            m.triangleCount = tri_count;
            m.vertexCount = v_count;

            m.vertices = (float*)RL_MALLOC(v_count * 3 * sizeof(float));
            m.normals = (float*)RL_MALLOC(v_count * 3 * sizeof(float));
            m.texcoords = (float*)RL_MALLOC(v_count * 2 * sizeof(float));
            m.colors = (unsigned char*)RL_MALLOC(v_count * 4 * sizeof(unsigned char));

            for (int ti = 0; ti < tri_count; ti++) {
                for (int c = 0; c < 3; c++) {
                    uint16_t vi = sm.indices[ti * 3 + c];
                    if (vi >= pm.positions.size()) vi = 0;

                    int out_idx = ti * 3 + c;
                    Vector3 pos3 = pm.positions[vi];
                    Vector3 norm3 = pm.normals[vi];
                    Vector2 uv2 = pm.uvs[vi];
                    Color col4 = pm.colors[vi];

                    m.vertices[out_idx * 3 + 0] = pos3.x;
                    m.vertices[out_idx * 3 + 1] = pos3.y;
                    m.vertices[out_idx * 3 + 2] = pos3.z;

                    m.normals[out_idx * 3 + 0] = norm3.x;
                    m.normals[out_idx * 3 + 1] = norm3.y;
                    m.normals[out_idx * 3 + 2] = norm3.z;

                    m.texcoords[out_idx * 2 + 0] = uv2.x;
                    m.texcoords[out_idx * 2 + 1] = uv2.y;

                    m.colors[out_idx * 4 + 0] = col4.r;
                    m.colors[out_idx * 4 + 1] = col4.g;
                    m.colors[out_idx * 4 + 2] = col4.b;
                    m.colors[out_idx * 4 + 3] = col4.a;

                    bmin.x = std::min(bmin.x, pos3.x);
                    bmin.y = std::min(bmin.y, pos3.y);
                    bmin.z = std::min(bmin.z, pos3.z);

                    bmax.x = std::max(bmax.x, pos3.x);
                    bmax.y = std::max(bmax.y, pos3.y);
                    bmax.z = std::max(bmax.z, pos3.z);
                }
            }

            if (IsWindowReady()) {
                UploadMesh(&m, false);
            }

            model->raylib_model.meshes[curr_mesh_idx] = m;
            model->raylib_model.meshMaterial[curr_mesh_idx] = sm.mat_idx;

            YmoSubmeshInfo sinfo;
            sinfo.submesh_index = (int)model->submeshes.size();
            sinfo.material_index = sm.mat_idx;
            sinfo.triangle_count = sm.tri_count;
            sinfo.vertex_start = sm.v_start;
            sinfo.vertex_count = sm.v_count;
            sinfo.raylib_mesh_index = curr_mesh_idx;
            model->submeshes.push_back(sinfo);

            model->total_vertices += v_count;
            model->total_triangles += tri_count;

            curr_mesh_idx++;
        }
    }

    if (bmin.x > bmax.x) {
        bmin = { -1, -1, -1 };
        bmax = { 1, 1, 1 };
    }

    model->bounds.min = bmin;
    model->bounds.max = bmax;
    model->center = Vector3Scale(Vector3Add(bmin, bmax), 0.5f);
    model->radius = Vector3Length(Vector3Subtract(bmax, model->center));
    model->raylib_model.transform = MatrixIdentity();
    model->is_loaded = true;

    return model;
}

void YmoLoader::bind_material_texture(ParsedYmoModel& model, int mat_idx, Texture2D texture) {
    if (mat_idx >= 0 && mat_idx < (int)model.materials.size()) {
        model.materials[mat_idx].texture = texture;
        model.materials[mat_idx].has_custom_texture = true;
    }
    if (mat_idx >= 0 && mat_idx < model.raylib_model.materialCount && model.raylib_model.materials && IsWindowReady()) {
        SetMaterialTexture(&model.raylib_model.materials[mat_idx], MATERIAL_MAP_DIFFUSE, texture);
    }
}

static Shader s_ymo_shader = {};
static int s_loc_alpha_test = -1;
static int s_loc_additive = -1;
static int s_loc_vertex_lighting = -1;
static bool s_shader_initialized = false;

static void ensure_ymo_shader() {
    if (s_shader_initialized || !IsWindowReady()) return;

    const char* vs_code =
        "#version 330\n"
        "layout(location = 0) in vec3 vertexPosition;\n"
        "layout(location = 1) in vec2 vertexTexCoord;\n"
        "layout(location = 2) in vec3 vertexNormal;\n"
        "layout(location = 3) in vec4 vertexColor;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "out vec2 fragTexCoord;\n"
        "out vec4 fragColor;\n"
        "out vec3 fragNormal;\n"
        "void main() {\n"
        "    fragTexCoord = vertexTexCoord;\n"
        "    fragColor = vertexColor;\n"
        "    fragNormal = normalize((matModel * vec4(vertexNormal, 0.0)).xyz);\n"
        "    gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
        "}\n";
    const char* fs_code =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec4 fragColor;\n"
        "in vec3 fragNormal;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform int uAlphaTest;\n"
        "uniform int uAdditive;\n"
        "uniform int uVertexLighting;\n"
        "out vec4 finalColor;\n"
        "void main() {\n"
        "    vec4 texelColor = texture(texture0, fragTexCoord);\n"
        "    vec4 vColor = (uVertexLighting == 1) ? fragColor : vec4(1.0, 1.0, 1.0, fragColor.a);\n"
        "    if (uAdditive == 1) {\n"
        "        // Additive light shaft (god rays / Z_ textures): vertex color multiplies texture\n"
        "        finalColor = texelColor * colDiffuse * vColor;\n"
        "    } else {\n"
        "        if (uAlphaTest == 1 && texelColor.a < 0.25) {\n"
        "            discard;\n"
        "        }\n"
        "        finalColor = texelColor * colDiffuse * vColor;\n"
        "    }\n"
        "}\n";

    s_ymo_shader = LoadShaderFromMemory(vs_code, fs_code);
    s_ymo_shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib(s_ymo_shader, "vertexPosition");
    s_ymo_shader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(s_ymo_shader, "vertexTexCoord");
    s_ymo_shader.locs[SHADER_LOC_VERTEX_NORMAL] = GetShaderLocationAttrib(s_ymo_shader, "vertexNormal");
    s_ymo_shader.locs[SHADER_LOC_VERTEX_COLOR] = GetShaderLocationAttrib(s_ymo_shader, "vertexColor");
    s_ymo_shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(s_ymo_shader, "mvp");
    s_ymo_shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s_ymo_shader, "matModel");
    s_ymo_shader.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(s_ymo_shader, "texture0");
    s_ymo_shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_ymo_shader, "colDiffuse");
    s_loc_alpha_test = GetShaderLocation(s_ymo_shader, "uAlphaTest");
    s_loc_additive = GetShaderLocation(s_ymo_shader, "uAdditive");
    s_loc_vertex_lighting = GetShaderLocation(s_ymo_shader, "uVertexLighting");
    s_shader_initialized = (s_ymo_shader.id > 0);
}

void YmoLoader::init_shaders() {
    ensure_ymo_shader();
}

static inline bool is_mat_additive(const YmoMaterialInfo* mat_info) {
    if (!mat_info) return false;
    std::string name_lower = mat_info->texture_name;
    for (auto& c : name_lower) c = (char)tolower((unsigned char)c);
    if (name_lower.find("zhikari") != std::string::npos || name_lower.find("z_") != std::string::npos) {
        return true;
    }
    if (mat_info->flags & 0x00000040) return true;
    return false;
}

static inline bool is_mat_transparent(const YmoMaterialInfo* mat_info) {
    if (!mat_info) return false;
    if (is_mat_additive(mat_info)) return false;
    if (mat_info->alpha < 0.98f && mat_info->alpha > 0.01f) return true;
    return false;
}

void YmoLoader::draw_model(ParsedYmoModel& model, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color tint, bool wireframe, bool untextured, bool vertex_lighting) {
    if (!model.is_loaded || !IsWindowReady()) return;
    static bool s_logged_first = false;
    if (!s_logged_first) {
        s_logged_first = true;
        fprintf(stderr, "[draw_model DEBUG] model=%s, meshCount=%d, center=(%.1f, %.1f, %.1f), rad=%.1f\n",
            model.filename.c_str(), model.raylib_model.meshCount, model.center.x, model.center.y, model.center.z, model.radius);
    }

    Matrix mat_t = MatrixTranslate(pos.x, pos.y, pos.z);
    Matrix mat_rx = MatrixRotateX(rot_rad.x);
    Matrix mat_ry = MatrixRotateY(rot_rad.y);
    Matrix mat_rz = MatrixRotateZ(rot_rad.z);
    Matrix mat_r = MatrixMultiply(MatrixMultiply(mat_rz, mat_rx), mat_ry);
    Matrix mat_s = MatrixScale(scale.x, scale.y, scale.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(mat_s, mat_r), mat_t);
    
    ensure_ymo_shader();

    static Material s_def_mat = LoadMaterialDefault();
    if (s_def_mat.shader.id == 0 && IsWindowReady()) {
        s_def_mat = LoadMaterialDefault();
    }

    rlDisableBackfaceCulling();
    rlEnableDepthTest();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(transform));

    // PASS 1: Opaque & Alpha-tested Cutout geometry (Foliage, Terrain, Architecture)
    // Depth write ON. Alpha < 0.25 is discarded so transparent areas do not pollute the Z-buffer.
    rlEnableDepthMask();
    rlSetBlendMode(BLEND_ALPHA);

    int alpha_test_on = 1;
    int additive_off = 0;
    int vert_light = vertex_lighting ? 1 : 0;
    if (s_shader_initialized && s_loc_alpha_test >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_alpha_test, &alpha_test_on, SHADER_UNIFORM_INT);
    }
    if (s_shader_initialized && s_loc_additive >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_additive, &additive_off, SHADER_UNIFORM_INT);
    }
    if (s_shader_initialized && s_loc_vertex_lighting >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_vertex_lighting, &vert_light, SHADER_UNIFORM_INT);
    }

    std::vector<int> transparent_pass_indices;

    for (int i = 0; i < model.raylib_model.meshCount; i++) {
        Mesh& mesh = model.raylib_model.meshes[i];
        if (mesh.triangleCount == 0 || mesh.vertexCount == 0) continue;

        if (mesh.vaoId == 0 && IsWindowReady()) {
            UploadMesh(&mesh, false);
        }
        if (mesh.vaoId == 0) continue;

        int mat_idx = model.raylib_model.meshMaterial[i];
        const YmoMaterialInfo* mat_info = (mat_idx >= 0 && mat_idx < (int)model.materials.size()) ? &model.materials[mat_idx] : nullptr;

        bool additive = is_mat_additive(mat_info);
        bool transparent = is_mat_transparent(mat_info);

        if (additive || transparent) {
            transparent_pass_indices.push_back(i);
            continue; // Defer to Pass 2
        }

        Material mat = (mat_idx >= 0 && mat_idx < model.raylib_model.materialCount) ? model.raylib_model.materials[mat_idx] : s_def_mat;
        if (s_shader_initialized) {
            mat.shader = s_ymo_shader;
        } else if (mat.shader.id == 0) {
            mat.shader = s_def_mat.shader;
        }

        if (mat_info && mat_info->has_custom_texture && mat_info->texture.id > 0 && !untextured) {
            mat.maps[MATERIAL_MAP_DIFFUSE].texture = mat_info->texture;
        } else if (mat.maps[MATERIAL_MAP_DIFFUSE].texture.id == 0 || untextured) {
            mat.maps[MATERIAL_MAP_DIFFUSE].texture = s_def_mat.maps[MATERIAL_MAP_DIFFUSE].texture;
        }
        mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
        DrawMesh(mesh, mat, MatrixIdentity());
    }

    // PASS 2: Semi-Transparent and Additive geometry (Water, Glass, Light Shafts / God Rays)
    // Depth write OFF so transparent and additive layers blend smoothly without occluding background geometry.
    if (!transparent_pass_indices.empty()) {
        rlDisableDepthMask();

        for (int i : transparent_pass_indices) {
            Mesh& mesh = model.raylib_model.meshes[i];
            int mat_idx = model.raylib_model.meshMaterial[i];
            const YmoMaterialInfo* mat_info = (mat_idx >= 0 && mat_idx < (int)model.materials.size()) ? &model.materials[mat_idx] : nullptr;

            bool additive = is_mat_additive(mat_info);

            Material mat = (mat_idx >= 0 && mat_idx < model.raylib_model.materialCount) ? model.raylib_model.materials[mat_idx] : s_def_mat;
            if (s_shader_initialized) {
                mat.shader = s_ymo_shader;
            } else if (mat.shader.id == 0) {
                mat.shader = s_def_mat.shader;
            }

            if (mat_info && mat_info->has_custom_texture && mat_info->texture.id > 0 && !untextured) {
                mat.maps[MATERIAL_MAP_DIFFUSE].texture = mat_info->texture;
            } else if (mat.maps[MATERIAL_MAP_DIFFUSE].texture.id == 0 || untextured) {
                mat.maps[MATERIAL_MAP_DIFFUSE].texture = s_def_mat.maps[MATERIAL_MAP_DIFFUSE].texture;
            }
            mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;

            int alpha_test_off = 0;
            int add_val = additive ? 1 : 0;
            if (s_shader_initialized && s_loc_alpha_test >= 0) {
                SetShaderValue(s_ymo_shader, s_loc_alpha_test, &alpha_test_off, SHADER_UNIFORM_INT);
            }
            if (s_shader_initialized && s_loc_additive >= 0) {
                SetShaderValue(s_ymo_shader, s_loc_additive, &add_val, SHADER_UNIFORM_INT);
            }
            if (s_shader_initialized && s_loc_vertex_lighting >= 0) {
                SetShaderValue(s_ymo_shader, s_loc_vertex_lighting, &vert_light, SHADER_UNIFORM_INT);
            }

            if (additive) {
                rlSetBlendMode(BLEND_ADDITIVE);
            } else {
                rlSetBlendMode(BLEND_ALPHA);
            }

            DrawMesh(mesh, mat, MatrixIdentity());
        }

        // Restore default state
        rlEnableDepthMask();
        rlSetBlendMode(BLEND_ALPHA);
    }

    // PASS 3: Wireframe Overlay (if enabled)
    // Render wireframe edges on top of the solid geometry with backface culling to avoid seeing inside/through front faces.
    if (wireframe) {
        rlEnableBackfaceCulling();
        rlEnableWireMode();
        rlDisableDepthMask();

        int alpha_test_off = 0;
        int add_off = 0;
        int vert_light_off = 0;
        if (s_shader_initialized && s_loc_alpha_test >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_alpha_test, &alpha_test_off, SHADER_UNIFORM_INT);
        }
        if (s_shader_initialized && s_loc_additive >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_additive, &add_off, SHADER_UNIFORM_INT);
        }
        if (s_shader_initialized && s_loc_vertex_lighting >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_vertex_lighting, &vert_light_off, SHADER_UNIFORM_INT);
        }

        Material wire_mat = s_def_mat;
        if (s_shader_initialized) {
            wire_mat.shader = s_ymo_shader;
        }
        wire_mat.maps[MATERIAL_MAP_DIFFUSE].texture = s_def_mat.maps[MATERIAL_MAP_DIFFUSE].texture;
        wire_mat.maps[MATERIAL_MAP_DIFFUSE].color = (tint.a < 250) ? tint : Color{ 0, 230, 255, 255 }; // Electric Azure

        for (int i = 0; i < model.raylib_model.meshCount; i++) {
            Mesh& mesh = model.raylib_model.meshes[i];
            if (mesh.triangleCount == 0 || mesh.vertexCount == 0 || mesh.vaoId == 0) continue;
            DrawMesh(mesh, wire_mat, MatrixIdentity());
        }

        rlDisableWireMode();
        rlDisableBackfaceCulling();
        rlEnableDepthMask();
    }

    rlPopMatrix();
}

void YmoLoader::draw_submesh(ParsedYmoModel& model, int submesh_idx, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color tint, bool wireframe, bool untextured, bool vertex_lighting) {
    if (!model.is_loaded || !IsWindowReady() || submesh_idx < 0 || submesh_idx >= (int)model.submeshes.size()) return;
    int mesh_idx = model.submeshes[submesh_idx].raylib_mesh_index;
    if (mesh_idx < 0 || mesh_idx >= model.raylib_model.meshCount) return;

    Mesh& mesh = model.raylib_model.meshes[mesh_idx];
    if (mesh.triangleCount == 0 || mesh.vertexCount == 0) return;

    if (mesh.vaoId == 0 && IsWindowReady()) {
        UploadMesh(&mesh, false);
    }
    if (mesh.vaoId == 0) return;

    ensure_ymo_shader();

    Matrix mat_t = MatrixTranslate(pos.x, pos.y, pos.z);
    Matrix mat_rx = MatrixRotateX(rot_rad.x);
    Matrix mat_ry = MatrixRotateY(rot_rad.y);
    Matrix mat_rz = MatrixRotateZ(rot_rad.z);
    Matrix mat_r = MatrixMultiply(MatrixMultiply(mat_rz, mat_rx), mat_ry);
    Matrix mat_s = MatrixScale(scale.x, scale.y, scale.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(mat_s, mat_r), mat_t);

    static Material s_def_mat = LoadMaterialDefault();
    if (s_def_mat.shader.id == 0 && IsWindowReady()) {
        s_def_mat = LoadMaterialDefault();
    }

    int mat_idx = model.raylib_model.meshMaterial[mesh_idx];
    const YmoMaterialInfo* mat_info = (mat_idx >= 0 && mat_idx < (int)model.materials.size()) ? &model.materials[mat_idx] : nullptr;
    bool additive = is_mat_additive(mat_info);
    bool transparent = is_mat_transparent(mat_info);

    Material mat = (mat_idx >= 0 && mat_idx < model.raylib_model.materialCount) ? model.raylib_model.materials[mat_idx] : s_def_mat;
    if (s_shader_initialized) {
        mat.shader = s_ymo_shader;
    } else if (mat.shader.id == 0) {
        mat.shader = s_def_mat.shader;
    }

    rlDisableBackfaceCulling();
    if (mat_info && mat_info->has_custom_texture && mat_info->texture.id > 0 && !untextured) {
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = mat_info->texture;
    } else if (mat.maps[MATERIAL_MAP_DIFFUSE].texture.id == 0 || untextured) {
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = s_def_mat.maps[MATERIAL_MAP_DIFFUSE].texture;
    }
    mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;

    rlEnableDepthTest();

    if (additive) {
        rlDisableDepthMask();
        rlSetBlendMode(BLEND_ADDITIVE);
    } else if (transparent) {
        rlDisableDepthMask();
        rlSetBlendMode(BLEND_ALPHA);
    } else {
        rlEnableDepthMask();
        rlSetBlendMode(BLEND_ALPHA);
    }

    int alpha_test_val = (!additive && !transparent) ? 1 : 0;
    int add_val = additive ? 1 : 0;
    int vert_light = vertex_lighting ? 1 : 0;
    if (s_shader_initialized && s_loc_alpha_test >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_alpha_test, &alpha_test_val, SHADER_UNIFORM_INT);
    }
    if (s_shader_initialized && s_loc_additive >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_additive, &add_val, SHADER_UNIFORM_INT);
    }
    if (s_shader_initialized && s_loc_vertex_lighting >= 0) {
        SetShaderValue(s_ymo_shader, s_loc_vertex_lighting, &vert_light, SHADER_UNIFORM_INT);
    }

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(transform));

    DrawMesh(mesh, mat, MatrixIdentity());

    if (wireframe) {
        rlEnableBackfaceCulling();
        rlEnableWireMode();
        rlDisableDepthMask();

        int alpha_test_off = 0;
        int add_off = 0;
        int vert_light_off = 0;
        if (s_shader_initialized && s_loc_alpha_test >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_alpha_test, &alpha_test_off, SHADER_UNIFORM_INT);
        }
        if (s_shader_initialized && s_loc_additive >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_additive, &add_off, SHADER_UNIFORM_INT);
        }
        if (s_shader_initialized && s_loc_vertex_lighting >= 0) {
            SetShaderValue(s_ymo_shader, s_loc_vertex_lighting, &vert_light_off, SHADER_UNIFORM_INT);
        }

        Material wire_mat = s_def_mat;
        if (s_shader_initialized) {
            wire_mat.shader = s_ymo_shader;
        }
        wire_mat.maps[MATERIAL_MAP_DIFFUSE].texture = s_def_mat.maps[MATERIAL_MAP_DIFFUSE].texture;
        wire_mat.maps[MATERIAL_MAP_DIFFUSE].color = (tint.a < 250) ? tint : Color{ 0, 230, 255, 255 };

        DrawMesh(mesh, wire_mat, MatrixIdentity());

        rlDisableWireMode();
        rlDisableBackfaceCulling();
        rlEnableDepthMask();
    }

    rlPopMatrix();

    rlEnableDepthMask();
    rlSetBlendMode(BLEND_ALPHA);
}
// ── YmoRegistry ─────────────────────────────────────────────────────────────

YmoRegistry& YmoRegistry::instance() {
    static YmoRegistry s_inst;
    return s_inst;
}

int YmoRegistry::register_model(std::shared_ptr<ParsedYmoModel> model) {
    if (!model) return -1;
    int h = m_next_handle++;
    m_models[h] = model;
    return h;
}

ParsedYmoModel* YmoRegistry::get_model(int handle) {
    auto it = m_models.find(handle);
    if (it != m_models.end()) return it->second.get();
    return nullptr;
}

void YmoRegistry::unregister_model(int handle) {
    m_models.erase(handle);
}

void YmoRegistry::clear_all() {
    m_models.clear();
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_ymo_load_from_memory(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);
    const char* fname = luaL_optstring(L, 2, "model.ymo");

    auto model = YmoLoader::load_from_memory((const uint8_t*)bytes, len, fname);
    if (!model) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to parse YMO model data");
        return 2;
    }

    int handle = YmoRegistry::instance().register_model(model);
    lua_pushinteger(L, handle);

    // Return info table
    lua_newtable(L);
    lua_pushstring(L, model->filename.c_str());
    lua_setfield(L, -2, "filename");
    lua_pushinteger(L, model->version);
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, model->total_vertices);
    lua_setfield(L, -2, "total_vertices");
    lua_pushinteger(L, model->total_triangles);
    lua_setfield(L, -2, "total_triangles");

    // Bounds
    lua_newtable(L);
    lua_pushnumber(L, model->bounds.min.x); lua_setfield(L, -2, "min_x");
    lua_pushnumber(L, model->bounds.min.y); lua_setfield(L, -2, "min_y");
    lua_pushnumber(L, model->bounds.min.z); lua_setfield(L, -2, "min_z");
    lua_pushnumber(L, model->bounds.max.x); lua_setfield(L, -2, "max_x");
    lua_pushnumber(L, model->bounds.max.y); lua_setfield(L, -2, "max_y");
    lua_pushnumber(L, model->bounds.max.z); lua_setfield(L, -2, "max_z");
    lua_pushnumber(L, model->center.x); lua_setfield(L, -2, "center_x");
    lua_pushnumber(L, model->center.y); lua_setfield(L, -2, "center_y");
    lua_pushnumber(L, model->center.z); lua_setfield(L, -2, "center_z");
    lua_pushnumber(L, model->radius); lua_setfield(L, -2, "radius");
    lua_setfield(L, -2, "bounds");

    // Materials list
    lua_newtable(L);
    for (size_t i = 0; i < model->materials.size(); i++) {
        const auto& mat = model->materials[i];
        lua_newtable(L);
        lua_pushinteger(L, mat.index);
        lua_setfield(L, -2, "index");
        lua_pushstring(L, mat.texture_path.c_str());
        lua_setfield(L, -2, "texture_path");
        lua_pushstring(L, mat.texture_name.c_str());
        lua_setfield(L, -2, "texture_name");
        lua_pushnumber(L, mat.alpha);
        lua_setfield(L, -2, "alpha");
        lua_pushinteger(L, mat.flags);
        lua_setfield(L, -2, "flags");
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "materials");

    // Submeshes list
    lua_newtable(L);
    for (size_t i = 0; i < model->submeshes.size(); i++) {
        const auto& sm = model->submeshes[i];
        lua_newtable(L);
        lua_pushinteger(L, sm.submesh_index);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, sm.material_index);
        lua_setfield(L, -2, "material_index");
        lua_pushinteger(L, sm.triangle_count);
        lua_setfield(L, -2, "triangle_count");
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "submeshes");

    // Collision refs
    lua_newtable(L);
    for (size_t i = 0; i < model->collision_files.size(); i++) {
        lua_pushstring(L, model->collision_files[i].c_str());
        lua_rawseti(L, -2, (int)i + 1);
    }
    lua_setfield(L, -2, "collision_files");

    return 2;
}

static int l_ymo_load_from_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    auto model = YmoLoader::load_from_file(path);
    if (!model) {
        lua_pushnil(L);
        lua_pushfstring(L, "Failed to load YMO file: %s", path);
        return 2;
    }
    int handle = YmoRegistry::instance().register_model(model);
    lua_pushinteger(L, handle);
    return 1;
}

static int l_ymo_draw(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    ParsedYmoModel* model = YmoRegistry::instance().get_model(h);
    if (!model) return 0;

    Vector3 pos = { (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0) };
    Vector3 rot = { (float)luaL_optnumber(L, 5, 0), (float)luaL_optnumber(L, 6, 0), (float)luaL_optnumber(L, 7, 0) };
    Vector3 scale = { (float)luaL_optnumber(L, 8, 1), (float)luaL_optnumber(L, 9, 1), (float)luaL_optnumber(L, 10, 1) };
    
    Color tint = WHITE;
    if (!lua_isnoneornil(L, 11)) {
        tint.r = (unsigned char)luaL_optinteger(L, 11, 255);
        tint.g = (unsigned char)luaL_optinteger(L, 12, 255);
        tint.b = (unsigned char)luaL_optinteger(L, 13, 255);
        tint.a = (unsigned char)luaL_optinteger(L, 14, 255);
    }
    bool wireframe = lua_toboolean(L, 15) != 0;
    bool untextured = lua_toboolean(L, 16) != 0;
    bool vertex_lighting = true;
    if (lua_gettop(L) >= 17 && !lua_isnil(L, 17)) {
        vertex_lighting = lua_toboolean(L, 17) != 0;
    }

    YmoLoader::draw_model(*model, pos, rot, scale, tint, wireframe, untextured, vertex_lighting);
    return 0;
}

static int l_ymo_draw_submesh(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int sm_idx = (int)luaL_checkinteger(L, 2);
    ParsedYmoModel* model = YmoRegistry::instance().get_model(h);
    if (!model) return 0;

    Vector3 pos = { (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0), (float)luaL_optnumber(L, 5, 0) };
    Vector3 rot = { (float)luaL_optnumber(L, 6, 0), (float)luaL_optnumber(L, 7, 0), (float)luaL_optnumber(L, 8, 0) };
    Vector3 scale = { (float)luaL_optnumber(L, 9, 1), (float)luaL_optnumber(L, 10, 1), (float)luaL_optnumber(L, 11, 1) };
    
    Color tint = WHITE;
    if (!lua_isnoneornil(L, 12)) {
        tint.r = (unsigned char)luaL_optinteger(L, 12, 255);
        tint.g = (unsigned char)luaL_optinteger(L, 13, 255);
        tint.b = (unsigned char)luaL_optinteger(L, 14, 255);
        tint.a = (unsigned char)luaL_optinteger(L, 15, 255);
    }
    bool wireframe = lua_toboolean(L, 16) != 0;
    bool untextured = lua_toboolean(L, 17) != 0;
    bool vertex_lighting = true;
    if (lua_gettop(L) >= 18 && !lua_isnil(L, 18)) {
        vertex_lighting = lua_toboolean(L, 18) != 0;
    }

    YmoLoader::draw_submesh(*model, sm_idx, pos, rot, scale, tint, wireframe, untextured, vertex_lighting);
    return 0;
}

static int l_ymo_bind_texture(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    int mat_idx = (int)luaL_checkinteger(L, 2);
    int tex_id = (int)luaL_checkinteger(L, 3);

    ParsedYmoModel* model = YmoRegistry::instance().get_model(h);
    if (!model) return 0;

    Texture2D tex = {};
    tex.id = (unsigned int)tex_id;
    tex.width = (int)luaL_optinteger(L, 4, 256);
    tex.height = (int)luaL_optinteger(L, 5, 256);
    tex.mipmaps = 1;
    tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    YmoLoader::bind_material_texture(*model, mat_idx, tex);
    return 0;
}

static int l_ymo_unload(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    YmoRegistry::instance().unregister_model(h);
    return 0;
}

void register_ymo_lua(lua_State* L) {
    lua_getglobal(L, "ys");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "ys");
        lua_getglobal(L, "ys");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_ymo_load_from_memory);
    lua_setfield(L, -2, "load_from_memory");
    lua_pushcfunction(L, l_ymo_load_from_file);
    lua_setfield(L, -2, "load_from_file");
    lua_pushcfunction(L, l_ymo_draw);
    lua_setfield(L, -2, "draw");
    lua_pushcfunction(L, l_ymo_draw_submesh);
    lua_setfield(L, -2, "draw_submesh");
    lua_pushcfunction(L, l_ymo_bind_texture);
    lua_setfield(L, -2, "bind_texture");
    lua_pushcfunction(L, l_ymo_unload);
    lua_setfield(L, -2, "unload");

    lua_setfield(L, -2, "ymo");
    lua_pop(L, 1); // pop "ys"
}

} // namespace falcom
