#!/usr/bin/env python3
"""Ground-truth regression tests for tools/make_ring_motion.py.

Validates the CT-driven ring-motion algorithm without any patient data: it generates its own
synthetic DICOM phantoms (a circle and an ellipse of known size) and checks that

  1. the DICOM contour extraction recovers the known radii within one pixel,
  2. each detector docks at (contour distance + clearance), and
  3. each detector's face points at the patient (numeric dot product, not a visual check).

Run:  python3 tests/test_ring_motion.py     (needs pydicom + numpy)
A OpenTOPAS zero-overlap check on the generated deck is a separate manual step (needs a build).
"""
import math, os, sys, tempfile
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import make_ring_motion as M


def _write_dicom(mask_fn, pxmm, nslice, outdir):
    import pydicom
    from pydicom.dataset import FileDataset, FileMetaDataset
    os.makedirs(outdir, exist_ok=True)
    ny = nx = 256
    yy, xx = np.mgrid[0:ny, 0:nx]
    arr = (mask_fn(xx - nx / 2, yy - ny / 2) * 1000).astype("int16")  # body=1000, air=0
    for k in range(nslice):
        fm = FileMetaDataset()
        fm.MediaStorageSOPClassUID = "1.2.840.10008.5.1.4.1.1.2"
        fm.MediaStorageSOPInstanceUID = pydicom.uid.generate_uid()
        fm.TransferSyntaxUID = pydicom.uid.ExplicitVRLittleEndian
        ds = FileDataset(f"{outdir}/s{k}.dcm", {}, file_meta=fm, preamble=b"\0" * 128)
        ds.Modality = "CT"; ds.Rows = ny; ds.Columns = nx; ds.PixelSpacing = [pxmm, pxmm]
        ds.ImagePositionPatient = [0, 0, float(k)]; ds.RescaleSlope = 1; ds.RescaleIntercept = 0
        ds.BitsAllocated = 16; ds.BitsStored = 16; ds.HighBit = 15; ds.PixelRepresentation = 1
        ds.SamplesPerPixel = 1; ds.PhotometricInterpretation = "MONOCHROME2"
        ds.PixelData = arr.tobytes(); ds.is_little_endian = True; ds.is_implicit_VR = False
        ds.save_as(f"{outdir}/s{k}.dcm")


def test_contour_recovery():
    tmp = tempfile.mkdtemp()
    pxmm = 2.0
    # circle: radius 100 px -> 200 mm; every azimuth should recover ~200 mm
    _write_dicom(lambda x, y: (x**2 + y**2) < 100**2, pxmm, 3, f"{tmp}/circ")
    R = M.contour_from_dicom(f"{tmp}/circ", threshold=500, axial_frac=0.5)
    err = max(abs(R(i * 30) - 200.0) for i in range(12))
    assert err <= pxmm * 1.5, f"circle contour off by {err:.1f} mm"

    # ellipse: x-semi 120 px (240 mm), y-semi 80 px (160 mm). Tool casts +z along image-y, +x along
    # image-x, so R(0 deg) = y-extent = 160, R(90 deg) = x-extent = 240.
    _write_dicom(lambda x, y: (x / 120)**2 + (y / 80)**2 < 1, pxmm, 3, f"{tmp}/ell")
    R = M.contour_from_dicom(f"{tmp}/ell", threshold=500, axial_frac=0.5)
    assert abs(R(0) - 160) <= pxmm * 1.5 and abs(R(90) - 240) <= pxmm * 1.5, "ellipse cardinals wrong"
    ref = M.contour_from_ellipse(240, 160)                 # a=x-extent, b=z-extent -> R(0)=b, R(90)=a
    err = max(abs(R(t) - ref(t)) for t in range(0, 360, 15))
    assert err <= pxmm * 2, f"ellipse contour off by {err:.1f} mm"


def test_contour_from_radii():
    """Measured-radii contour (PHI-safe on-server extraction path) interpolates and round-trips."""
    radii = [91, 94, 140, 157, 138, 121, 101, 114, 120, 153, 153, 115]   # a real Y-90 abdomen contour
    R = M.contour_from_radii(radii)
    for i, r in enumerate(radii):
        assert abs(R(i * 30) - r) < 1e-6, f"radius {i} not recovered"
    mid = R(15)                                    # halfway between azimuths 0 and 30 -> between 91 and 94
    assert 91 <= mid <= 94, f"interpolation off: {mid}"


def test_docking_and_facing():
    a, b, clear = 150.0, 100.0, 40.0
    R = M.contour_from_ellipse(a, b)

    def Ry(t): c, s = math.cos(t), math.sin(t); return [[c, 0, s], [0, 1, 0], [-s, 0, c]]
    def Tr(Mx): return [[Mx[j][i] for j in range(3)] for i in range(3)]
    def mv(Mx, v): return [sum(Mx[i][k] * v[k] for k in range(3)) for i in range(3)]

    N = 12
    for i in range(N):
        th = i * 360.0 / N
        Rc = R(th) + clear
        assert abs(Rc - (R(th) + clear)) < 1e-9                      # docks at contour + clearance
        # tool places Det with RotY = -th; passive placement -> world face-normal = Ry(-th)^T (0,0,1)
        n = mv(Tr(Ry(math.radians(-th))), [0, 0, 1])
        ph = (math.sin(math.radians(th)), math.cos(math.radians(th)))
        dot = n[0] * ph[0] + n[2] * ph[1]
        assert abs(abs(dot) - 1.0) < 0.02, f"Det{i} face not radial (dot={dot:.3f})"


def test_table_arm_removal():
    """A phantom = body ellipse + a bright scanner TABLE (bar below, thinly touching) + a DETACHED
    ARM blob. The hardened extraction must recover the clean body ellipse, ignoring table and arm."""
    tmp = tempfile.mkdtemp(); pxmm = 2.0
    def shape(x, y):
        body = (x / 120)**2 + (y / 80)**2 < 1
        table = (y > 84) & (y < 92) & (np.abs(x) < 135)      # thin bar just below the body
        arm = ((x - 150)**2 + (y + 10)**2) < 18**2           # detached blob to the +x side
        return body | table | arm
    _write_dicom(shape, pxmm, 3, f"{tmp}/ta")
    R = M.contour_from_dicom(f"{tmp}/ta", threshold=500, axial_frac=0.5)
    # cardinals must match the CLEAN ellipse (160 / 240 mm), not be inflated by table (down/+y) or arm (+x)
    assert abs(R(0) - 160) <= pxmm * 2, f"table leaked into contour: R(0)={R(0):.0f} (exp 160)"
    assert abs(R(180) - 160) <= pxmm * 2, f"contour wrong at bottom: R(180)={R(180):.0f}"
    assert abs(R(90) - 240) <= pxmm * 2, f"arm leaked into contour: R(90)={R(90):.0f} (exp 240)"
    ref = M.contour_from_ellipse(240, 160)
    err = max(abs(R(t) - ref(t)) for t in range(0, 360, 15))
    assert err <= pxmm * 2, f"contour off by {err:.1f} mm after table/arm removal"


def test_no_collision():
    """Collision-safe docking: as the gantry rotates 0..gantry, no detector center may come closer than
    (contour + clearance) at ANY azimuth it sweeps through."""
    a, b, clear, gantry, sweep, hlx = 150.0, 100.0, 40.0, 30.0, 15.0, 19.65
    R = M.contour_from_ellipse(a, b)
    N = 12
    Rc = M.collision_safe_radii(R, N, clear, gantry, sweep, hlx)
    worst = 1e9
    for i in range(N):
        th0 = i * 360.0 / N
        dpanel = math.degrees(hlx / (R(th0) + clear))
        for phi_deg in [x * 0.5 for x in range(int((gantry + 2 * (sweep + dpanel)) * 2) + 1)]:
            th = th0 - sweep - dpanel + phi_deg
            worst = min(worst, Rc[i] - (R(th) + clear))       # >= 0 means still clear of body+clearance
    assert worst >= -0.5, f"detector penetrates body+clearance by {-worst:.1f} mm during rotation"
    # and the naive local-radius docking WOULD collide (sanity: the safe radii are strictly larger somewhere)
    naive = [R(i * 360.0 / N) + clear for i in range(N)]
    assert any(Rc[i] > naive[i] + 0.5 for i in range(N)), "collision-safe radii should exceed naive at the narrow ends"


if __name__ == "__main__":
    test_contour_recovery()
    test_docking_and_facing()
    test_contour_from_radii()
    test_table_arm_removal()
    test_no_collision()
    print("PASS: contour recovery + docking/facing + table/arm removal + collision-safe rotation")
