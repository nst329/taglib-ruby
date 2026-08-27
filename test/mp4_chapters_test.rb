# frozen-string-literal: true

require File.join(File.dirname(__FILE__), 'helper')
require 'fileutils'
require 'tmpdir'

class MP4ChaptersTest < Test::Unit::TestCase
  SAMPLE_FILE = 'test/data/mp4.m4a'

  def with_copy
    path = File.join(Dir.tmpdir, "taglib-mp4-chapters-#{Process.pid}.m4a")
    FileUtils.cp(SAMPLE_FILE, path)
    yield path
  ensure
    FileUtils.rm_f(path)
  end

  def chapter_list
    [
      TagLib::MP4::Chapter.new(start_time: 0, title: 'Opening'),
      TagLib::MP4::Chapter.new(start_time: 500, title: 'Main')
    ]
  end

  def test_empty_file_and_preserve_default
    with_copy do |path|
      file = TagLib::MP4::File.new(path, false)
      assert_equal :none, file.chapter_style
      assert_empty file.chapters

      file.set_chapters(chapter_list)
      assert_equal :both, file.chapter_style
      assert_equal chapter_list, file.chapters
      assert file.save_chapters

      file.close
      file = TagLib::MP4::File.new(path, false)
      assert_equal :both, file.chapter_style
      assert_equal chapter_list, file.chapters
      file.close
    end
  end

  def test_style_specific_write_and_remove
    with_copy do |path|
      file = TagLib::MP4::File.new(path, false)
      file.set_chapters(chapter_list, style: :nero)
      assert_equal :nero, file.chapter_style
      assert file.save_chapters
      file.close

      file = TagLib::MP4::File.new(path, false)
      assert_equal chapter_list, file.nero_chapters
      assert_empty file.quicktime_chapters
      file.remove_chapters(style: :nero)
      assert_equal :none, file.chapter_style
      assert file.save_chapters
      file.close
    end
  end

  def test_invalid_chapters_raise_argument_error
    assert_raise(ArgumentError) { TagLib::MP4::Chapter.new(start_time: -1, title: 'bad') }
    assert_raise(ArgumentError) { TagLib::MP4::Chapter.new(start_time: 0, title: "bad\0") }

    with_copy do |path|
      file = TagLib::MP4::File.new(path, false)
      first = TagLib::MP4::Chapter.new(start_time: 0, title: 'a')
      second = TagLib::MP4::Chapter.new(start_time: 0, title: 'b')
      assert_raise(ArgumentError) { file.set_chapters([first, second]) }
      file.close
    end
  end

  def test_save_chapters_preserves_existing_tag_and_artwork
    with_copy do |path|
      file = TagLib::MP4::File.new(path, false)
      original = [file.tag.title, file.tag.artist, file.tag.item_map['covr'].to_cover_art_list.size]
      file.set_chapters(chapter_list)
      file.save_chapters
      file.close

      file = TagLib::MP4::File.new(path, false)
      assert_equal original, [file.tag.title, file.tag.artist, file.tag.item_map['covr'].to_cover_art_list.size]
      file.close
    end
  end
end
