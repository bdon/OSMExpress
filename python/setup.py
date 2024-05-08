import setuptools

with open("README.md", "r") as fh:
    long_description = fh.read()

requirements = [
    'lmdb~=1.4.1',
    'pycapnp~=2.0.0',
]

setuptools.setup(
    name='osmx',
    version='0.0.5',
    author="Brandon Liu",
    author_email='brandon@protomaps.com',
    description='Read OSM Express (.osmx) database files.',
    license="BSD-2-Clause",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/protomaps/OSMExpress",
    packages=setuptools.find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: BSD License",
        "Operating System :: OS Independent",
    ],
    install_requires = requirements,
    requires_python='>=3.0',
    package_data={'osmx':['messages.capnp']}
)
