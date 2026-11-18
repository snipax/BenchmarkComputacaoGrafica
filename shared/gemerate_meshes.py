import math
from pathlib import Path

# Configuracao exata: (nome_arquivo, slices_S, stacks_R, triangulos_esperados)
MESH_SPECS = [
    ("planet_500.obj",   25,  11,   500),
    ("planet_2500.obj",  50,  26,   2500),
    ("planet_10000.obj", 100, 51,   10000),
    ("planet_40000.obj", 200, 101,  40000),
]

def generate_uv_sphere_obj(filepath: Path, slices: int, stacks: int, radius: float = 1.0) -> tuple[int, int]:
    """
    Gera uma esfera UV triangulada no padrao Wavefront OBJ com posicao (v),
    mapeamento UV (vt) e normal unitaria (vn) por vertice.
    Formula topologica: N_tri = 2 * slices * (stacks - 1)
    """
    vertices = []
    uvs = []
    normals = []
    faces = []

    # Geracao dos vertices, UVs e normais
    for i in range(stacks + 1):
        v_coord = i / float(stacks)
        phi = v_coord * math.pi  # Latitude: 0 (polo norte) ate pi (polo sul)

        sin_phi = math.sin(phi)
        cos_phi = math.cos(phi)

        for j in range(slices + 1):
            u_coord = j / float(slices)
            theta = u_coord * (2.0 * math.pi)  # Longitude: 0 ate 2*pi

            sin_theta = math.sin(theta)
            cos_theta = math.cos(theta)

            # Vetor normal unitario na superficie esferica
            nx = sin_phi * cos_theta
            ny = cos_phi
            nz = sin_phi * sin_theta

            # Posicao espacial escalada pelo raio
            px = radius * nx
            py = radius * ny
            pz = radius * nz

            vertices.append((px, py, pz))
            uvs.append((u_coord, 1.0 - v_coord))
            normals.append((nx, ny, nz))

    # Geracao dos indices de faces triangulares (1-based no formato OBJ)
    stride = slices + 1
    for i in range(stacks):
        for j in range(slices):
            k1 = i * stride + j + 1
            k2 = (i + 1) * stride + j + 1

            # Calota superior (i == 0): 1 triangulo por setor
            if i != 0:
                faces.append((k1, k2, k1 + 1))

            # Calota inferior (i == stacks - 1): 1 triangulo por setor
            # Aneis intermediarios: 2 triangulos por setor
            if i != (stacks - 1):
                faces.append((k1 + 1, k2, k2 + 1))

    # Escrita estruturada do arquivo .obj
    with filepath.open("w", encoding="utf-8") as f:
        f.write("# Benchmark Computacao Grafica - Esfera UV Parametrica\n")
        f.write(f"# Slices: {slices} | Stacks: {stacks} | Triangulos: {len(faces)}\n")
        f.write("o PlanetSphere\n")

        for vx, vy, vz in vertices:
            f.write(f"v {vx:.6f} {vy:.6f} {vz:.6f}\n")

        for u, v in uvs:
            f.write(f"vt {u:.6f} {v:.6f}\n")

        for nx, ny, nz in normals:
            f.write(f"vn {nx:.6f} {ny:.6f} {nz:.6f}\n")

        f.write("s 1\n")
        for v1, v2, v3 in faces:
            f.write(f"f {v1}/{v1}/{v1} {v2}/{v2}/{v2} {v3}/{v3}/{v3}\n")

    return len(vertices), len(faces)


def main():
    base_dir = Path(__file__).resolve().parent
    meshes_dir = base_dir / "meshes"
    meshes_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 65)
    print(f"{'ARQUIVO':<20} | {'VERTICES':<10} | {'TRIANGULOS':<12} | {'STATUS'}")
    print("=" * 65)

    for filename, slices, stacks, expected_tris in MESH_SPECS:
        out_path = meshes_dir / filename
        num_verts, num_tris = generate_uv_sphere_obj(out_path, slices, stacks)
        status = "OK (EXATO)" if num_tris == expected_tris else f"ERRO ({num_tris})"
        print(f"{filename:<20} | {num_verts:<10} | {num_tris:<12} | {status}")

    print("=" * 65)
    print(f"Malhas exportadas em: {meshes_dir}")


if __name__ == "__main__":
    main()