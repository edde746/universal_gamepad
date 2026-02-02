Pod::Spec.new do |s|
  s.name             = 'universal_gamepad'
  s.version          = '0.1.0'
  s.summary          = 'iOS implementation of the gamepad Flutter plugin.'
  s.description      = <<-DESC
Cross-platform Flutter plugin providing unified gamepad input using the
GameController framework on iOS.
                       DESC
  s.homepage         = 'https://github.com/edde/gamepad'
  s.license          = { :type => 'MIT', :file => '../LICENSE' }
  s.author           = { 'gamepad' => 'dev@gamepads-plus.dev' }
  s.source           = { :http => 'https://github.com/edde/gamepad' }

  s.platform         = :ios, '14.0'
  s.swift_version    = '5.0'

  s.source_files     = 'Classes/**/*'

  s.dependency 'Flutter'

  s.frameworks       = 'GameController'

  # Flutter.framework does not contain a i386 slice.
  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
end
