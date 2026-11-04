models/s64/robot_cloth
{
 cull none
 { clampmap models/s64/robot_cloth.png
 alphaFunc GE128
 depthWrite
 rgbGen vertex
 }
}
models/s64/robot_cloth_join
{
 cull none
 { map models/s64/robot_cloth.png
 rgbGen vertex
 }
}
models/s64/robot
{
 cull none
 { map models/s64/robot.jpg
 rgbGen lightingDiffuse
 }
 { stage normalMap
 map models/s64/robot_n.jpg
 }
 { stage specularMap
 map models/s64/robot_s.tga
 }
 { map models/s64/robot_e.png
 blendFunc GL_ONE GL_ONE
 rgbGen identity
 }
}
models/s64/robot_pbr
{
 cull none
 { map models/s64/robot.jpg
 rgbGen lightingDiffuse
 }
 { stage normalMap
 map models/s64/robot_n.jpg
 }
 { stage specularMap
 map models/s64/robot_pbr.png
 }
 { map models/s64/robot_e.png
 blendFunc GL_ONE GL_ONE
 rgbGen identity
 }
}
