#ifndef TAGLIB_MP4_CHAPTERS_GENERATED_H
#define TAGLIB_MP4_CHAPTERS_GENERATED_H

#include <cstdlib>
#include <cstring>
#include <taglib/mp4chapter.h>
#include <taglib/mp4nerochapterlist.h>
#include <taglib/mp4qtchapterlist.h>

enum TagLibRubyMP4ChapterStyle {
  TAGLIB_RUBY_MP4_STYLE_NONE = 0,
  TAGLIB_RUBY_MP4_STYLE_NERO = 1,
  TAGLIB_RUBY_MP4_STYLE_QUICKTIME = 2,
  TAGLIB_RUBY_MP4_STYLE_BOTH = 3,
  TAGLIB_RUBY_MP4_STYLE_ANY = 4
};

static VALUE taglib_mp4_chapter_class() {
  return rb_path2class("TagLib::MP4::Chapter");
}

static VALUE taglib_mp4_chapter_to_ruby(const TagLib::MP4::Chapter &chapter) {
  VALUE title = taglib_string_to_ruby_string(chapter.title());
  VALUE start_time = LL2NUM(chapter.startTime());
  return rb_funcall(taglib_mp4_chapter_class(), rb_intern("new"), 2, start_time, title);
}

static VALUE taglib_mp4_chapters_to_ruby(const TagLib::MP4::ChapterList &chapters) {
  VALUE result = rb_ary_new2(chapters.size());
  for (TagLib::MP4::ChapterList::ConstIterator it = chapters.begin(); it != chapters.end(); ++it) {
    rb_ary_push(result, taglib_mp4_chapter_to_ruby(*it));
  }
  return result;
}

static TagLib::MP4::ChapterList taglib_mp4_chapters_from_ruby(VALUE value, TagLib::MP4::File *file) {
  Check_Type(value, T_ARRAY);
  TagLib::MP4::ChapterList result;
  long long previous_start_time = -1;
  TagLib::AudioProperties *properties = file->audioProperties();
  const int length = properties ? properties->lengthInMilliseconds() : 0;

  for (long i = 0; i < RARRAY_LEN(value); ++i) {
    VALUE entry = rb_ary_entry(value, i);
    if (!rb_obj_is_kind_of(entry, taglib_mp4_chapter_class())) {
      rb_raise(rb_eArgError, "chapter must be a TagLib::MP4::Chapter");
    }
    VALUE start_value = rb_funcall(entry, rb_intern("start_time"), 0);
    long long start_time = NUM2LL(start_value);
    if (!RB_INTEGER_TYPE_P(start_value) || start_time < 0 ||
        (i > 0 && start_time <= previous_start_time) ||
        (length > 0 && start_time > length)) {
      rb_raise(rb_eArgError, "chapter start_time is invalid");
    }
    VALUE title = rb_funcall(entry, rb_intern("title"), 0);
    Check_Type(title, T_STRING);
    if (rb_enc_get_index(title) != rb_utf8_encindex() ||
        !RTEST(rb_funcall(title, rb_intern("valid_encoding?"), 0))) {
      rb_raise(rb_eArgError, "chapter title must be valid UTF-8");
    }
    if (memchr(RSTRING_PTR(title), '\0', RSTRING_LEN(title)) != NULL) {
      rb_raise(rb_eArgError, "chapter title must not contain NUL bytes");
    }
    result.append(TagLib::MP4::Chapter(ruby_string_to_taglib_string(title), start_time));
    previous_start_time = start_time;
  }
  return result;
}

static bool taglib_mp4_chapter_lists_equal(const TagLib::MP4::ChapterList &left,
                                           const TagLib::MP4::ChapterList &right) {
  if (left.size() != right.size()) return false;
  TagLib::MP4::ChapterList::ConstIterator lit = left.begin();
  TagLib::MP4::ChapterList::ConstIterator rit = right.begin();
  for (; lit != left.end(); ++lit, ++rit) {
    if (lit->title() != rit->title() || std::llabs(lit->startTime() - rit->startTime()) > 1) {
      return false;
    }
  }
  return true;
}

static TagLib::MP4::ChapterList taglib_mp4_read_nero(TagLib::MP4::File *file, bool *present = NULL) {
  TagLib::MP4::NeroChapterList holder;
  bool exists = holder.read(file);
  if (present) *present = exists;
  return holder.chapters();
}

static TagLib::MP4::ChapterList taglib_mp4_read_quicktime(TagLib::MP4::File *file, bool *present = NULL) {
  TagLib::MP4::QtChapterList holder;
  bool exists = holder.read(file);
  if (present) *present = exists;
  return holder.chapters();
}

static VALUE taglib_mp4_chapter_style(TagLib::MP4::File *file) {
  bool nero = false, quicktime = false;
  taglib_mp4_read_nero(file, &nero);
  taglib_mp4_read_quicktime(file, &quicktime);
  if (nero && quicktime) return ID2SYM(rb_intern("both"));
  if (nero) return ID2SYM(rb_intern("nero"));
  if (quicktime) return ID2SYM(rb_intern("quicktime"));
  return ID2SYM(rb_intern("none"));
}

static VALUE taglib_mp4_chapters(TagLib::MP4::File *file, int style) {
  if (style == TAGLIB_RUBY_MP4_STYLE_NERO) return taglib_mp4_chapters_to_ruby(file->neroChapters());
  if (style == TAGLIB_RUBY_MP4_STYLE_QUICKTIME) return taglib_mp4_chapters_to_ruby(file->qtChapters());
  bool nero_present = false, quicktime_present = false;
  TagLib::MP4::ChapterList nero = taglib_mp4_read_nero(file, &nero_present);
  TagLib::MP4::ChapterList quicktime = taglib_mp4_read_quicktime(file, &quicktime_present);
  if (nero_present && quicktime_present && !taglib_mp4_chapter_lists_equal(nero, quicktime)) {
    rb_raise(rb_path2class("TagLib::MP4::ChapterConflictError"), "Nero and QuickTime chapters differ");
  }
  return taglib_mp4_chapters_to_ruby(nero_present ? nero : quicktime);
}

static void taglib_mp4_set_chapters(TagLib::MP4::File *file, VALUE value, int style) {
  TagLib::MP4::ChapterList chapters = taglib_mp4_chapters_from_ruby(value, file);
  if (style == TAGLIB_RUBY_MP4_STYLE_NERO || style == TAGLIB_RUBY_MP4_STYLE_BOTH) file->setNeroChapters(chapters);
  if (style == TAGLIB_RUBY_MP4_STYLE_QUICKTIME || style == TAGLIB_RUBY_MP4_STYLE_BOTH) file->setQtChapters(chapters);
}

static bool taglib_mp4_remove_chapters(TagLib::MP4::File *file, int style) {
  TagLib::MP4::ChapterList empty;
  if (style == TAGLIB_RUBY_MP4_STYLE_NERO || style == TAGLIB_RUBY_MP4_STYLE_BOTH) {
    file->setNeroChapters(empty);
  }
  if (style == TAGLIB_RUBY_MP4_STYLE_QUICKTIME || style == TAGLIB_RUBY_MP4_STYLE_BOTH) {
    file->setQtChapters(empty);
  }
  return true;
}

static bool taglib_mp4_save_chapter_list(TagLib::MP4::File *file,
                                         const TagLib::MP4::ChapterList &desired, bool nero) {
  bool present = false;
  TagLib::MP4::ChapterList current = nero ? taglib_mp4_read_nero(file, &present) : taglib_mp4_read_quicktime(file, &present);
  if (present && taglib_mp4_chapter_lists_equal(current, desired)) return true;
  if (desired.isEmpty()) {
    if (nero) { TagLib::MP4::NeroChapterList holder; return holder.remove(file); }
    TagLib::MP4::QtChapterList holder; return holder.remove(file);
  }
  if (nero) { TagLib::MP4::NeroChapterList holder; holder.setChapters(desired); return holder.write(file); }
  TagLib::MP4::QtChapterList holder; holder.setChapters(desired); return holder.write(file);
}

static VALUE taglib_mp4_save_chapters(TagLib::MP4::File *file) {
  if (file->readOnly() || !file->isValid()) rb_raise(rb_path2class("TagLib::MP4::ChapterSaveError"), "MP4 file is not writable or valid");
  bool nero_present = false, quicktime_present = false;
  TagLib::MP4::ChapterList nero = taglib_mp4_read_nero(file, &nero_present);
  TagLib::MP4::ChapterList quicktime = taglib_mp4_read_quicktime(file, &quicktime_present);
  TagLib::MP4::ChapterList desired_nero = file->neroChapters();
  TagLib::MP4::ChapterList desired_quicktime = file->qtChapters();
  bool nero_changed = !taglib_mp4_chapter_lists_equal(nero, desired_nero) || nero_present != !desired_nero.isEmpty();
  bool quicktime_changed = !taglib_mp4_chapter_lists_equal(quicktime, desired_quicktime) || quicktime_present != !desired_quicktime.isEmpty();
  if (nero_changed && !taglib_mp4_save_chapter_list(file, desired_nero, true)) rb_raise(rb_path2class("TagLib::MP4::ChapterSaveError"), "failed to save Nero chapters");
  if (quicktime_changed && !taglib_mp4_save_chapter_list(file, desired_quicktime, false)) rb_raise(rb_path2class("TagLib::MP4::ChapterSaveError"), "failed to save QuickTime chapters");
  return Qtrue;
}

#endif
