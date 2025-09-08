# Submitting Sofi to AUR

## Prerequisites
1. Create an AUR account at https://aur.archlinux.org/register
2. Add your SSH public key to your AUR account

## Steps to Submit

### 1. Create GitHub Release
```bash
git tag v1.0.0
git push origin v1.0.0
```
Then go to https://github.com/adsofi/sofi/releases and create a release from the tag.

### 2. Update PKGBUILD SHA256
After creating the release, get the real SHA256:
```bash
curl -L https://github.com/adsofi/sofi/archive/v1.0.0.tar.gz | sha256sum
```
Update the sha256sums in PKGBUILD with the actual value.

### 3. Test PKGBUILD Locally
```bash
makepkg -si
```
This will build and install the package to test it works.

### 4. Generate .SRCINFO
```bash
makepkg --printsrcinfo > .SRCINFO
```

### 5. Clone AUR Repository
```bash
git clone ssh://aur@aur.archlinux.org/sofi.git aur-sofi
cd aur-sofi
```

### 6. Add PKGBUILD and .SRCINFO
```bash
cp ../PKGBUILD .
cp ../.SRCINFO .
git add PKGBUILD .SRCINFO
git commit -m "Initial commit: sofi 1.0.0"
git push
```

## Congratulations!
Your package is now in the AUR! Users can install it with:
```bash
paru -S sofi
# or
yay -S sofi
```

## Maintenance
When you release new versions:
1. Update pkgver in PKGBUILD
2. Update sha256sums
3. Regenerate .SRCINFO
4. Commit and push to AUR